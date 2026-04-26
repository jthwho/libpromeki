/**
 * @file      thread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <thread>
#include <promeki/objectbase.h>
#include <promeki/set.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/atomic.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Scheduling policy for thread priority control.
 * @ingroup events
 *
 * Wraps the OS-level scheduling policies.  Not all policies are
 * available on every platform — unsupported values map to Default.
 */
enum class SchedulePolicy {
        Default,        ///< Normal time-sharing (SCHED_OTHER on POSIX).
        RoundRobin,     ///< Real-time round-robin (SCHED_RR).
        Fifo,           ///< Real-time first-in-first-out (SCHED_FIFO).
        Batch,          ///< Batch scheduling, Linux only (SCHED_BATCH).
        Idle            ///< Idle scheduling, Linux only (SCHED_IDLE).
};

/**
 * @brief Wrapper around std::thread with a built-in EventLoop.
 *
 * Derives from ObjectBase so it participates in parent/child tree and
 * signal/slot.  Operates in two modes:
 *
 * 1. **Spawned thread** — start() creates a std::thread, builds an
 *    EventLoop on it, and calls run().  The Thread object itself lives
 *    on the creating thread.
 *
 * 2. **Adopted thread** — wraps an already-running thread (e.g. main).
 *    No std::thread is owned.  start() is a no-op.  Created by
 *    adoptCurrentThread().
 *
 * @par Thread Safety
 * The Thread object itself is thread-affine to its owning thread
 * (typically the creator).  @c start, @c quit, @c wait, @c setName,
 * @c setPriority etc. should be invoked from the owner.  The
 * @c threadEventLoop() pointer is safe to use cross-thread because
 * @ref EventLoop's @c postCallable / @c postEvent are themselves
 * thread-safe.  Static helpers (@c current, @c osThreadId,
 * @c hardwareConcurrency) may be called from any thread.
 */
class Thread : public ObjectBase {
        PROMEKI_OBJECT(Thread, ObjectBase)
        public:
                /** @brief Platform-specific native thread handle type. */
                using NativeHandle = std::thread::native_handle_type;
                /**
                 * @brief Adopts the calling thread as a Thread object.
                 *
                 * Used by Application for the main thread.  The returned
                 * Thread is not owned by anyone — the caller manages its
                 * lifetime.
                 *
                 * @return A new Thread representing the current thread.
                 */
                static Thread *adoptCurrentThread();

                /**
                 * @brief Returns the Thread object for the calling thread.
                 * @return The current thread's Thread, or nullptr if none.
                 */
                static Thread *currentThread();

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
                 * Convenience for threads not managed by a Thread object
                 * (e.g. ThreadPool workers).  Sets the OS-level thread
                 * name (visible in debuggers, @c htop, @c ps&nbsp;-L) and
                 * updates the Logger's cached thread name.
                 *
                 * On Linux/macOS the name is silently truncated to 15
                 * characters by the OS.
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
                 * Wraps std::thread::hardware_concurrency().  Returns 0 if
                 * the value cannot be determined.
                 *
                 * @return The number of concurrent threads supported.
                 */
                static unsigned int idealThreadCount();

                /**
                 * @brief Constructs a Thread (spawned mode).
                 * @param parent Optional parent ObjectBase.
                 */
                Thread(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Waits for the thread if still running. */
                virtual ~Thread();

                /**
                 * @brief Starts the thread.
                 *
                 * Creates an EventLoop on the new thread and calls run().
                 * No-op for adopted threads or if already running.
                 *
                 * @param stackSize The stack size in bytes for the new thread.
                 *        Zero (the default) uses the system default stack size.
                 *        Ignored for adopted threads.
                 */
                void start(size_t stackSize = 0);

                /**
                 * @brief Waits for the thread to finish.
                 *
                 * For spawned threads, blocks until the thread exits or the
                 * timeout expires.  If the timeout expires the thread is
                 * still running and the caller may retry or call quit()
                 * first.
                 *
                 * @param timeoutMs Maximum wait time in milliseconds.
                 *        Zero (the default) waits indefinitely.
                 * @return Error::Ok on success, Error::Invalid for adopted
                 *         threads, Error::Timeout if the wait timed out.
                 */
                Error wait(unsigned int timeoutMs = 0);

                /**
                 * @brief Returns the EventLoop associated with this thread.
                 *
                 * For spawned threads: valid after start(), before wait().
                 * For adopted threads: returns the EventLoop currently
                 * active on that thread (looked up lazily, so it is valid
                 * as soon as an EventLoop is created on the adopted
                 * thread).
                 *
                 * @return The thread's EventLoop, or nullptr.
                 */
                EventLoop *threadEventLoop() const;

                /**
                 * @brief Requests the thread's event loop to quit.
                 * @param returnCode The exit code for the event loop.
                 */
                void quit(int returnCode = 0);

                /**
                 * @brief Returns the exit code from the thread's event loop.
                 *
                 * Only meaningful after the thread has finished.
                 *
                 * @return The exit code, or 0 if not yet available.
                 */
                int exitCode() const { return _exitCode.value(); }

                /**
                 * @brief Returns whether the thread is currently running.
                 * @return @c true if the thread is running.
                 */
                bool isRunning() const { return _running.value(); }

                /**
                 * @brief Returns whether this is an adopted thread.
                 * @return @c true for adopted threads, @c false for spawned.
                 */
                bool isAdopted() const { return _adopted; }

                /**
                 * @brief Returns whether the calling thread is this thread.
                 * @return @c true if the caller is running on this thread.
                 */
                bool isCurrentThread() const { return _currentThread == this; }

                /**
                 * @brief Returns the OS-native thread ID.
                 *
                 * For spawned threads this is captured at thread start.
                 * For adopted threads it is captured at adoption time.
                 * Returns 0 if the thread has not yet started.
                 *
                 * @return The native thread ID as a 64-bit unsigned integer.
                 */
                uint64_t nativeId() const { return _nativeId.value(); }

                /**
                 * @brief Returns the C++ std::thread::id of this Thread.
                 *
                 * Complements @ref nativeId for code that wants to
                 * compare against @c std::this_thread::get_id()
                 * without caring about OS-level TID representation.
                 * Captured at thread start (spawned mode) or at
                 * adoption time (adopted mode); before either, a
                 * default-constructed @c std::thread::id is returned.
                 *
                 * @return The std::thread::id for this thread.
                 */
                std::thread::id id() const { return _stdId.value(); }

                /**
                 * @brief Returns the current scheduling policy of this thread.
                 *
                 * The thread must be running.  Returns SchedulePolicy::Default
                 * if the thread is not running or on error.
                 *
                 * @return The current scheduling policy.
                 */
                SchedulePolicy schedulePolicy() const;

                /**
                 * @brief Returns the current priority of this thread.
                 *
                 * The thread must be running.  Returns 0 if the thread is
                 * not running or on error.
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
                 * @param priority The priority value.  Use priorityMin() and
                 *        priorityMax() to determine the valid range for the
                 *        given policy.
                 * @param policy The scheduling policy to apply.
                 * @return Error::Ok on success, Error::Invalid if the thread
                 *         is not running, Error::LibraryFailure on OS-level
                 *         failure.
                 */
                Error setPriority(int priority,
                                  SchedulePolicy policy = SchedulePolicy::Default);

                /**
                 * @brief Returns the name of this thread.
                 *
                 * Returns the name previously set by setName(), or an empty
                 * string if no name has been set.
                 *
                 * @return The thread name.
                 */
                String name() const;

                /**
                 * @brief Sets the name of this thread.
                 *
                 * The name is always stored locally.  On supported platforms
                 * the OS-level thread name is also updated so that the name
                 * appears in debuggers and system tools (e.g. `top -H`,
                 * `htop`, `ps -L`).
                 *
                 * If called before start(), the name is applied when the
                 * thread starts.  The OS name is silently truncated to the
                 * platform limit (typically 15 characters on Linux/macOS).
                 *
                 * On macOS the OS name can only be set from the thread
                 * itself, so cross-thread setName() updates the local name
                 * but not the OS name.
                 *
                 * The logger's cached thread name is automatically updated
                 * when setName() is called from the thread itself.
                 *
                 * @param name The name to assign.
                 */
                void setName(const String &name);

                /**
                 * @brief Returns the set of CPU cores this thread is allowed to run on.
                 *
                 * The thread must be running.  Returns an empty set if the
                 * thread is not running, on error, or if the platform does
                 * not support CPU affinity queries.
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
                 * @return Error::Ok on success, Error::Invalid if the thread
                 *         is not running, Error::LibraryFailure on OS-level
                 *         failure, Error::NotSupported on unsupported platforms.
                 */
                Error setAffinity(const Set<int> &cpus);

                /** @brief Emitted when the thread starts running.
                 *  @signal */
                PROMEKI_SIGNAL(started);

                /** @brief Emitted when the thread finishes (carries the event loop exit code).
                 *  @signal */
                PROMEKI_SIGNAL(finished, int);

        protected:
                /**
                 * @brief Override for custom thread behavior.
                 *
                 * Default implementation runs the thread's EventLoop via
                 * exec().
                 */
                virtual void run();

        private:
                static thread_local Thread      *_currentThread;

                std::thread             _thread;
                Atomic<bool>            _running;
                Atomic<int>             _exitCode;
                Atomic<uint64_t>        _nativeId;
                Atomic<std::thread::id> _stdId;
                String                  _name;
                EventLoop               *_threadLoop = nullptr;
                mutable Mutex           _mutex;
                WaitCondition           _startedCv;
                WaitCondition           _finishedCv;
                bool                    _started = false;
                bool                    _finished = false;
                bool                    _adopted = false;
                bool                    _usesPthread = false;
                NativeHandle _pthreadHandle{};

                /**
                 * @brief Returns the native thread handle for priority operations.
                 *
                 * For spawned threads returns the std::thread native handle.
                 * For adopted threads returns the calling thread's handle
                 * (pthread_self() on POSIX).
                 *
                 * @return The platform-specific thread handle.
                 */
                NativeHandle nativeHandle() const;
                bool isJoinable() const;
                void joinThread();
                void applyOsName();
                void threadEntry();
};

PROMEKI_NAMESPACE_END
