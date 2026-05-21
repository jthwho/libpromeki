/**
 * @file      basicthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>

#include <promeki/basicthread.h>
#include <promeki/atomic.h>
#include <promeki/logger.h>
#include <promeki/mutex.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
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

} // namespace

struct BasicThread::Data {
                String                  name;
                std::thread             thread;
                Atomic<bool>            running;
                Atomic<uint64_t>        nativeId;
                Atomic<std::thread::id> stdId;
                NativeHandle            pthreadHandle{};
                bool                    usesPthread = false;
                mutable Mutex           mutex;
};

uint64_t BasicThread::currentNativeId() {
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

void BasicThread::setCurrentThreadName(const String &name) {
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
}

int BasicThread::priorityMin(SchedulePolicy policy) {
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

int BasicThread::priorityMax(SchedulePolicy policy) {
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

unsigned int BasicThread::idealThreadCount() {
        return std::thread::hardware_concurrency();
}

void BasicThread::sleepNs(int64_t ns) {
        if (ns <= 0) return;
        std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

void BasicThread::sleepUs(int64_t us) {
        if (us <= 0) return;
        std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void BasicThread::sleepMs(int64_t ms) {
        if (ms <= 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void BasicThread::yield() {
        std::this_thread::yield();
}

BasicThread::BasicThread() : d(UniquePtr<Data>::create()) {
        return;
}

BasicThread::BasicThread(const String &name) : d(UniquePtr<Data>::create()) {
        d->name = name;
        return;
}

BasicThread::BasicThread(BasicThread &&other) noexcept : d(std::move(other.d)) {
        return;
}

BasicThread &BasicThread::operator=(BasicThread &&other) noexcept {
        if (this == &other) return *this;
        if (isJoinable()) join();
        d = std::move(other.d);
        return *this;
}

BasicThread::~BasicThread() {
        if (!d) return;
        if (isJoinable()) join();
        return;
}

Error BasicThread::start(Entry entry, size_t stackSize) {
        if (!d) {
                promekiWarn("BasicThread::start refused: thread state is null");
                return Error::Invalid;
        }
        if (!entry) {
                promekiWarn("BasicThread::start('%s') refused: empty entry function",
                            d->name.cstr());
                return Error::Invalid;
        }
        if (isJoinable()) {
                promekiWarn("BasicThread::start('%s') refused: already joinable", d->name.cstr());
                return Error::Busy;
        }

        // Capture a raw Data* — the BasicThread destructor joins before
        // the UniquePtr<Data> goes away, so the pointer is always live
        // for as long as the entry function is on the stack.
        Data *data = d.get();
        auto  wrapped = [data, entry = std::move(entry)]() mutable {
                data->nativeId.setValue(currentNativeId());
                data->stdId.setValue(std::this_thread::get_id());
                data->running.setValue(true);
                String startName;
                {
                        Mutex::Locker locker(data->mutex);
                        startName = data->name;
                }
                if (!startName.isEmpty()) setCurrentThreadName(startName);
                entry();
                data->running.setValue(false);
        };

#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (stackSize > 0) {
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setstacksize(&attr, stackSize);

                // Heap-own the closure so pthread_create's C trampoline
                // can take ownership via a raw void *.  UniquePtr makes
                // the ownership transfer explicit: we release into the
                // trampoline on success, the trampoline reseats it into
                // its own UniquePtr and lets it expire when the entry
                // function returns; on pthread_create failure we let
                // the original UniquePtr destroy the closure.
                auto ctx = UniquePtr<Function<void()>>::create(std::move(wrapped));
                pthread_t handle = 0;
                int       ret = pthread_create(
                        &handle, &attr,
                        [](void *arg) -> void * {
                                auto owned = UniquePtr<Function<void()>>::takeOwnership(
                                        static_cast<Function<void()> *>(arg));
                                (*owned)();
                                return nullptr;
                        },
                        ctx.get());
                pthread_attr_destroy(&attr);
                if (ret != 0) {
                        promekiWarn("BasicThread::start('%s') pthread_create failed: rc=%d (%s)",
                                    d->name.cstr(), ret, ::strerror(ret));
                        return Error::LibraryFailure;
                }
                (void)ctx.release();
                d->pthreadHandle = handle;
                d->usesPthread = true;
                return Error::Ok;
        }
#else
        (void)stackSize;
#endif
        try {
                d->thread = std::thread(std::move(wrapped));
        } catch (const std::exception &e) {
                promekiWarn("BasicThread::start('%s') std::thread ctor threw: %s",
                            d->name.cstr(), e.what());
                return Error::LibraryFailure;
        } catch (...) {
                promekiWarn("BasicThread::start('%s') std::thread ctor threw unknown exception",
                            d->name.cstr());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error BasicThread::join() {
        if (!d) return Error::Ok;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (d->usesPthread) {
                int rc = pthread_join(d->pthreadHandle, nullptr);
                d->usesPthread = false;
                d->pthreadHandle = {};
                if (rc != 0) {
                        promekiWarn("BasicThread::join('%s') pthread_join failed: rc=%d (%s)",
                                    d->name.cstr(), rc, ::strerror(rc));
                        return Error::LibraryFailure;
                }
                return Error::Ok;
        }
#endif
        if (!d->thread.joinable()) return Error::Ok;
        try {
                d->thread.join();
        } catch (const std::exception &e) {
                promekiWarn("BasicThread::join('%s') threw: %s", d->name.cstr(), e.what());
                return Error::LibraryFailure;
        } catch (...) {
                promekiWarn("BasicThread::join('%s') threw unknown exception", d->name.cstr());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

bool BasicThread::isJoinable() const {
        if (!d) return false;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (d->usesPthread) return true;
#endif
        return d->thread.joinable();
}

bool BasicThread::isRunning() const {
        return d ? d->running.value() : false;
}

bool BasicThread::isCurrentThread() const {
        if (!d) return false;
        return std::this_thread::get_id() == d->stdId.value();
}

String BasicThread::name() const {
        if (!d) return {};
        Mutex::Locker locker(d->mutex);
        return d->name;
}

void BasicThread::setName(const String &name) {
        if (!d) return;
        // Capture handle + name + ownership state under the lock, then
        // make the OS call without it — pthread_setname_np / Logger
        // post do not need the lock and avoid any nested-lock risk.
        String       nameCopy;
        NativeHandle target{};
        bool         haveTarget = false;
        bool         fromSelf = false;
        {
                Mutex::Locker locker(d->mutex);
                d->name = name;
                if (name.isEmpty() || !isJoinable()) return;
                nameCopy = name;
                fromSelf = (std::this_thread::get_id() == d->stdId.value());
                if (!fromSelf) {
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
                        if (d->usesPthread) {
                                target = d->pthreadHandle;
                                haveTarget = true;
                        } else if (d->thread.joinable()) {
                                target = const_cast<std::thread &>(d->thread).native_handle();
                                haveTarget = true;
                        }
#elif defined(PROMEKI_PLATFORM_WINDOWS)
                        if (d->thread.joinable()) {
                                target = const_cast<std::thread &>(d->thread).native_handle();
                                haveTarget = true;
                        }
#endif
                }
        }
        if (fromSelf) {
                // Self-naming path: setCurrentThreadName handles both
                // the OS-level name and the Logger's per-thread cache
                // (keyed by the calling thread's TID — which is us).
                setCurrentThreadName(nameCopy);
                return;
        }
        if (!haveTarget) return;
        // Cross-thread naming: update the OS name only.  The Logger
        // cache is keyed by caller TID, so updating it here would
        // attribute the new name to the wrong thread; see the
        // doc-comment on setName.
#if defined(PROMEKI_PLATFORM_LINUX)
        pthread_setname_np(target, nameCopy.cstr());
#elif defined(PROMEKI_PLATFORM_APPLE)
        // macOS only allows setting the name of the current thread.
        (void)target;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        int len = MultiByteToWideChar(CP_UTF8, 0, nameCopy.cstr(), -1, nullptr, 0);
        if (len > 0) {
                std::wstring wide(len, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, nameCopy.cstr(), -1, wide.data(), len);
                SetThreadDescription(target, wide.c_str());
        }
#endif
        return;
}

BasicThread::NativeHandle BasicThread::nativeHandle() const {
        if (!d) return {};
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        if (d->usesPthread) return d->pthreadHandle;
        return const_cast<std::thread &>(d->thread).native_handle();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        return const_cast<std::thread &>(d->thread).native_handle();
#else
        return {};
#endif
}

uint64_t BasicThread::nativeId() const {
        return d ? d->nativeId.value() : 0;
}

std::thread::id BasicThread::id() const {
        return d ? d->stdId.value() : std::thread::id{};
}

SchedulePolicy BasicThread::schedulePolicy() const {
        if (!d || !d->running.value()) return SchedulePolicy::Default;
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

int BasicThread::priority() const {
        if (!d || !d->running.value()) return 0;
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        int                policy = 0;
        struct sched_param param;
        if (pthread_getschedparam(nativeHandle(), &policy, &param) != 0) return 0;
        return param.sched_priority;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        return GetThreadPriority(nativeHandle());
#else
        return 0;
#endif
}

Error BasicThread::setPriority(int prio, SchedulePolicy policy) {
        if (!d || !d->running.value()) {
                promekiWarn("BasicThread::setPriority(prio=%d) refused: thread not running", prio);
                return Error::Invalid;
        }
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE)
        struct sched_param param;
        param.sched_priority = prio;
        int ret = pthread_setschedparam(nativeHandle(), schedulePolicyToNative(policy), &param);
        if (ret != 0) {
                promekiWarn("BasicThread::setPriority('%s', prio=%d) pthread_setschedparam failed: "
                            "rc=%d (%s) — RLIMIT_RTPRIO?",
                            d->name.cstr(), prio, ret, ::strerror(ret));
                return Error::LibraryFailure;
        }
        return Error::Ok;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)policy;
        if (!SetThreadPriority(nativeHandle(), prio)) {
                promekiWarn("BasicThread::setPriority(prio=%d) SetThreadPriority failed: err=%lu",
                            prio, (unsigned long)GetLastError());
                return Error::LibraryFailure;
        }
        return Error::Ok;
#else
        (void)prio;
        (void)policy;
        promekiWarnOnce("BasicThread::setPriority refused: not supported on this platform");
        return Error::NotSupported;
#endif
}

Set<int> BasicThread::affinity() const {
        Set<int> result;
        if (!d || !d->running.value()) return result;
#if defined(PROMEKI_PLATFORM_LINUX)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (pthread_getaffinity_np(nativeHandle(), sizeof(cpuset), &cpuset) == 0) {
                int ncpus = static_cast<int>(CPU_SETSIZE);
                for (int i = 0; i < ncpus; i++) {
                        if (CPU_ISSET(i, &cpuset)) result.insert(i);
                }
        }
#endif
        return result;
}

Error BasicThread::setAffinity(const Set<int> &cpus) {
        if (!d || !d->running.value()) {
                promekiWarn("BasicThread::setAffinity refused: thread not running");
                return Error::Invalid;
        }
#if defined(PROMEKI_PLATFORM_LINUX)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (cpus.isEmpty()) {
                int ncpus = static_cast<int>(idealThreadCount());
                for (int i = 0; i < ncpus; i++) CPU_SET(i, &cpuset);
        } else {
                for (int cpu : cpus) CPU_SET(cpu, &cpuset);
        }
        int ret = pthread_setaffinity_np(nativeHandle(), sizeof(cpuset), &cpuset);
        if (ret != 0) {
                promekiWarn("BasicThread::setAffinity('%s', %zu cpus) pthread_setaffinity_np failed: "
                            "rc=%d (%s)",
                            d->name.cstr(), cpus.size(), ret, ::strerror(ret));
                return Error::LibraryFailure;
        }
        return Error::Ok;
#elif defined(PROMEKI_PLATFORM_APPLE)
        (void)cpus;
        promekiWarnOnce("BasicThread::setAffinity refused: not supported on Apple platforms");
        return Error::NotSupported;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)cpus;
        promekiWarnOnce("BasicThread::setAffinity refused: not implemented on Windows");
        return Error::NotSupported;
#else
        (void)cpus;
        promekiWarnOnce("BasicThread::setAffinity refused: not supported on this platform");
        return Error::NotSupported;
#endif
}

PROMEKI_NAMESPACE_END
