/**
 * @file      thread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <sched.h>
#elif defined(PROMEKI_PLATFORM_APPLE)
#include <pthread.h>
#include <sched.h>
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        int schedulePolicyToNative(SchedulePolicy policy) {
                switch (policy) {
                        case SchedulePolicy::RoundRobin: return SCHED_RR;
                        case SchedulePolicy::Fifo: return SCHED_FIFO;
#if defined(PROMEKI_PLATFORM_LINUX)
                        case SchedulePolicy::Batch: return SCHED_BATCH;
                        case SchedulePolicy::Idle: return SCHED_IDLE;
#endif
                        default: return SCHED_OTHER;
                }
        }

        SchedulePolicy nativeToSchedulePolicy(int policy) {
                switch (policy) {
                        case SCHED_RR: return SchedulePolicy::RoundRobin;
                        case SCHED_FIFO: return SchedulePolicy::Fifo;
#if defined(PROMEKI_PLATFORM_LINUX)
                        case SCHED_BATCH: return SchedulePolicy::Batch;
                        case SCHED_IDLE: return SchedulePolicy::Idle;
#endif
                        default: return SchedulePolicy::Default;
                }
        }
#endif

} // anonymous namespace

thread_local Thread *Thread::_currentThread = nullptr;

uint64_t Thread::currentNativeId() {
#if defined(PROMEKI_PLATFORM_LINUX)
        return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(PROMEKI_PLATFORM_APPLE)
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        return tid;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        return static_cast<uint64_t>(GetCurrentThreadId());
#else
        return 0;
#endif
}

void Thread::setCurrentThreadName(const String &name) {
        if (name.isEmpty()) return;
#if defined(PROMEKI_PLATFORM_LINUX)
        pthread_setname_np(pthread_self(), name.cstr());
#elif defined(PROMEKI_PLATFORM_APPLE)
        pthread_setname_np(name.cstr());
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        int len = MultiByteToWideChar(CP_UTF8, 0, name.cstr(), -1, nullptr, 0);
        if (len > 0) {
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, name.cstr(), -1, wide.data(), len);
                SetThreadDescription(GetCurrentThread(), wide.c_str());
        }
#endif
        Logger::setThreadName(name);
        return;
}

int Thread::priorityMin(SchedulePolicy policy) {
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        return sched_get_priority_min(schedulePolicyToNative(policy));
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)policy;
        return THREAD_PRIORITY_IDLE;
#else
        (void)policy;
        return 0;
#endif
}

int Thread::priorityMax(SchedulePolicy policy) {
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        return sched_get_priority_max(schedulePolicyToNative(policy));
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)policy;
        return THREAD_PRIORITY_TIME_CRITICAL;
#else
        (void)policy;
        return 0;
#endif
}

Thread *Thread::adoptCurrentThread() {
        Thread *t = new Thread();
        t->_adopted = true;
        t->_running.setValue(true);
        t->_nativeId.setValue(currentNativeId());
        t->_stdId.setValue(std::this_thread::get_id());
        // Cache the calling thread's current EventLoop now so that
        // cross-thread callers of threadEventLoop() (e.g. the signal
        // watcher thread invoking Application::mainEventLoop() on a
        // SIGINT) get a valid pointer without first having to be
        // primed by a same-thread query.
        t->_threadLoop = EventLoop::current();
        _currentThread = t;
        return t;
}

Thread *Thread::currentThread() {
        return _currentThread;
}

Thread::Thread(ObjectBase *parent) : ObjectBase(parent) {
        return;
}

Thread::~Thread() {
        if (!_adopted) {
                // Send a quit unconditionally — no-op if the loop never
                // came up or has already drained.  Idempotent so callers
                // do not need to coordinate with an explicit prior
                // shutdown.
                quit();

                // Block until threadEntry() observably finishes before
                // unwinding any base-class state.  threadEntry() drops
                // _running to false *before* it sets _finished, so
                // isJoinable() can return false while the worker is
                // still mid-finishedSignal emit / mutex-locked
                // _finished=true write.  If we joined the OS handle in
                // that window the join would synchronize on thread
                // exit, but the destructor body would have already
                // returned to the base class — vtable sliced — while
                // the worker was still making virtual calls and
                // touching *this*.  Waiting on _finished closes that
                // window without depending on the platform handle's
                // joinable status.
                if (isJoinable() || _running.value()) {
                        _mutex.lock();
                        while (!_finished) _finishedCv.wait(_mutex);
                        _mutex.unlock();
                }
                if (isJoinable()) joinThread();
        }
        if (_currentThread == this) _currentThread = nullptr;
        return;
}

void Thread::start(size_t stackSize) {
        if (_adopted) return;
        if (_running.value()) return;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (stackSize > 0) {
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setstacksize(&attr, stackSize);
                pthread_t handle;
                int       ret = pthread_create(
                        &handle, &attr,
                        [](void *arg) -> void       *{
                                static_cast<Thread *>(arg)->threadEntry();
                                return nullptr;
                        },
                        this);
                pthread_attr_destroy(&attr);
                if (ret != 0) return;
                _pthreadHandle = handle;
                _usesPthread = true;
        } else {
                _thread = std::thread(&Thread::threadEntry, this);
        }
#else
        (void)stackSize;
        _thread = std::thread(&Thread::threadEntry, this);
#endif
        // Wait until the thread has set up its EventLoop
        _mutex.lock();
        _startedCv.wait(_mutex, [this] { return _started; });
        _mutex.unlock();
        return;
}

Error Thread::wait(unsigned int timeoutMs) {
        if (_adopted) return Error::Invalid;
        if (!isJoinable()) return Error::Ok;
        if (timeoutMs == 0) {
                joinThread();
        } else {
                _mutex.lock();
                Error err = _finishedCv.wait(_mutex, [this] { return _finished; }, timeoutMs);
                _mutex.unlock();
                if (err != Error::Ok) return Error::Timeout;
                joinThread();
        }
        // Reset state so the Thread object could be started again
        _started = false;
        _finished = false;
        _usesPthread = false;
        return Error::Ok;
}

EventLoop *Thread::threadEventLoop() const {
        // Take @c _mutex for every read/write of @c _threadLoop so the
        // adopted-thread lazy cache write below races neither with
        // cross-thread readers (e.g. a signal-watcher thread asking
        // for the main thread's loop) nor with the spawned-thread
        // teardown path in @c threadEntry.
        Mutex::Locker locker(_mutex);
        if (_adopted) {
                // Cache the EventLoop when called from the adopted thread
                // so that cross-thread callers see the correct pointer.
                if (_currentThread == this) {
                        EventLoop *loop = EventLoop::current();
                        if (loop != nullptr) {
                                const_cast<Thread *>(this)->_threadLoop = loop;
                        }
                }
        }
        return _threadLoop;
}

void Thread::quit(int returnCode) {
        // Take @c _mutex so the worker cannot tear down @c _threadLoop
        // mid-call.  Adopted threads never set or clear @c _threadLoop
        // through @c threadEntry, so the lock is only relevant for
        // spawned workers — but it is harmless either way.
        Mutex::Locker locker(_mutex);
        if (_threadLoop != nullptr) {
                _threadLoop->quit(returnCode);
        }
        return;
}

void Thread::run() {
        if (_threadLoop != nullptr) {
                _threadLoop->exec();
        }
        return;
}

unsigned int Thread::idealThreadCount() {
        return std::thread::hardware_concurrency();
}

bool Thread::isJoinable() const {
        if (_usesPthread) return _running.value() || _finished;
        return _thread.joinable();
}

void Thread::joinThread() {
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (_usesPthread) {
                pthread_join(_pthreadHandle, nullptr);
                return;
        }
#endif
        _thread.join();
}

Thread::NativeHandle Thread::nativeHandle() const {
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (_adopted) return pthread_self();
        if (_usesPthread) return _pthreadHandle;
        return const_cast<std::thread &>(_thread).native_handle();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        if (_adopted) return GetCurrentThread();
        return const_cast<std::thread &>(_thread).native_handle();
#else
        return {};
#endif
}

SchedulePolicy Thread::schedulePolicy() const {
        if (!_running.value()) return SchedulePolicy::Default;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        int                policy = 0;
        struct sched_param param;
        if (pthread_getschedparam(nativeHandle(), &policy, &param) != 0) {
                return SchedulePolicy::Default;
        }
        return nativeToSchedulePolicy(policy);
#else
        return SchedulePolicy::Default;
#endif
}

int Thread::priority() const {
        if (!_running.value()) return 0;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        int                policy = 0;
        struct sched_param param;
        if (pthread_getschedparam(nativeHandle(), &policy, &param) != 0) {
                return 0;
        }
        return param.sched_priority;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        return GetThreadPriority(nativeHandle());
#else
        return 0;
#endif
}

String Thread::name() const {
        Mutex::Locker locker(_mutex);
        return _name;
}

void Thread::setName(const String &n) {
        {
                Mutex::Locker locker(_mutex);
                _name = n;
        }
        if (_running.value()) {
                applyOsName();
        }
        return;
}

void Thread::applyOsName() {
        Mutex::Locker locker(_mutex);
        if (_name.isEmpty()) return;
        // When called from the thread itself (always the case during
        // threadEntry() startup), delegate to setCurrentThreadName()
        // which uses pthread_self() — safe even before the parent has
        // finished storing _thread / _pthreadHandle.
        const bool fromSelf = (_currentThread == this);
        if (fromSelf) {
                setCurrentThreadName(_name);
                return;
        }
        // Cross-thread naming — use the stored native handle.
#if defined(PROMEKI_PLATFORM_LINUX)
        pthread_setname_np(nativeHandle(), _name.cstr());
#elif defined(PROMEKI_PLATFORM_APPLE)
        // macOS only allows setting the name of the current thread.
        // Cross-thread naming is not supported.
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE target = nativeHandle();
        int    len = MultiByteToWideChar(CP_UTF8, 0, _name.cstr(), -1, nullptr, 0);
        if (len > 0) {
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, _name.cstr(), -1, wide.data(), len);
                SetThreadDescription(target, wide.c_str());
        }
#endif
        return;
}

Set<int> Thread::affinity() const {
        Set<int> result;
        if (!_running.value()) return result;
#if defined(PROMEKI_PLATFORM_LINUX)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (pthread_getaffinity_np(nativeHandle(), sizeof(cpuset), &cpuset) == 0) {
                int ncpus = static_cast<int>(CPU_SETSIZE);
                for (int i = 0; i < ncpus; i++) {
                        if (CPU_ISSET(i, &cpuset)) result.insert(i);
                }
        }
#elif defined(PROMEKI_PLATFORM_APPLE)
        // macOS does not support pthread_getaffinity_np.
        // Return empty set to indicate unknown affinity.
#endif
        return result;
}

Error Thread::setAffinity(const Set<int> &cpus) {
        if (!_running.value()) return Error::Invalid;
#if defined(PROMEKI_PLATFORM_LINUX)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (cpus.isEmpty()) {
                // Empty set: allow all CPUs
                int ncpus = static_cast<int>(std::thread::hardware_concurrency());
                for (int i = 0; i < ncpus; i++) CPU_SET(i, &cpuset);
        } else {
                for (int cpu : cpus) CPU_SET(cpu, &cpuset);
        }
        int ret = pthread_setaffinity_np(nativeHandle(), sizeof(cpuset), &cpuset);
        return ret == 0 ? Error::Ok : Error::LibraryFailure;
#elif defined(PROMEKI_PLATFORM_APPLE)
        // macOS uses thread affinity tags (thread_policy_set with
        // THREAD_AFFINITY_POLICY) which are hints, not hard bindings.
        // Not implemented — return NotSupported.
        (void)cpus;
        return Error::NotSupported;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)cpus;
        return Error::NotSupported;
#else
        (void)cpus;
        return Error::NotSupported;
#endif
}

Error Thread::setPriority(int prio, SchedulePolicy policy) {
        if (!_running.value()) return Error::Invalid;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        struct sched_param param;
        param.sched_priority = prio;
        int ret = pthread_setschedparam(nativeHandle(), schedulePolicyToNative(policy), &param);
        return ret == 0 ? Error::Ok : Error::LibraryFailure;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)policy;
        return SetThreadPriority(nativeHandle(), prio) ? Error::Ok : Error::LibraryFailure;
#else
        (void)prio;
        (void)policy;
        return Error::NotSupported;
#endif
}

void Thread::threadEntry() {
        _currentThread = this;
        _nativeId.setValue(currentNativeId());
        _stdId.setValue(std::this_thread::get_id());

        // Heap-allocate the worker's EventLoop so its destruction can
        // be sequenced explicitly under @c _mutex.  A stack EventLoop
        // is destroyed during natural function unwind — outside any
        // lock — and that races with external callers of @c quit /
        // @c threadEventLoop that are dereferencing @c _threadLoop on
        // another thread.
        UniquePtr<EventLoop> loop = UniquePtr<EventLoop>::create();
        {
                Mutex::Locker locker(_mutex);
                _threadLoop = loop.get();
        }
        _running.setValue(true);
        applyOsName();

        // Signal the creating thread that we're ready
        {
                Mutex::Locker locker(_mutex);
                _started = true;
        }
        _startedCv.wakeOne();

        startedSignal.emit();
        run();
        _exitCode.setValue(loop->exitCode());

        // Emit finished while the EventLoop is still alive so that
        // cross-thread signal dispatch from this thread still works.
        finishedSignal.emit(_exitCode.value());

        _running.setValue(false);

        // Hold @c _mutex while clearing @c _threadLoop AND tearing
        // down the loop.  External callers (e.g. another thread
        // calling @c Thread::quit) take the same lock and therefore
        // either observe a non-null @c _threadLoop with the loop
        // still alive (we are holding them out) or a null pointer
        // after the loop has been destroyed.  Notify @c _finished
        // inside the same critical section so a waiter that re-enters
        // observes both side effects atomically.
        {
                Mutex::Locker locker(_mutex);
                _threadLoop = nullptr;
                loop.clear();
                _finished = true;
        }
        _finishedCv.wakeAll();
        return;
}

PROMEKI_NAMESPACE_END
