/**
 * @file      threadpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/threadpool.h>

#include <algorithm>
#include <ctime>

#include <promeki/logger.h>
#include <promeki/thread.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(ThreadPool)

namespace {
/// Reserved id used by @ref ThreadPool::WorkStats for tasks
/// submitted via the untagged @c submit overload.  Distinct from
/// any real registered tag id (which is FNV-1a hashed and never
/// equals 0).
constexpr uint64_t kUntaggedId = 0;

/// Returns the calling thread's @c CLOCK_THREAD_CPUTIME_ID in
/// nanoseconds.  POSIX guarantees this is per-thread CPU time
/// (user + kernel), so the delta around a callable measures only
/// what *that* thread burned — no contamination from other
/// threads in the pool.
int64_t threadCpuNs() {
        struct timespec ts;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
        return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(ts.tv_nsec);
}
}  // namespace

Mutex &ThreadPool::registryMutex() {
        static Mutex m;
        return m;
}

List<ThreadPool *> &ThreadPool::registry() {
        static List<ThreadPool *> r;
        return r;
}

List<ThreadPool *> ThreadPool::allPools() {
        Mutex::Locker locker(registryMutex());
        return registry();
}

ThreadPool::ThreadPool(int maxThreadCount, bool lazy) {
        int count = maxThreadCount;
        if (count < 0) {
                count = static_cast<int>(std::thread::hardware_concurrency());
                if (count <= 0) count = 1;
        }
        _maxThreadCount = count;
        if (!lazy && count > 0) spawnThreads(count);
        {
                Mutex::Locker locker(registryMutex());
                registry().pushToBack(this);
        }
        promekiDebug("ThreadPool(%p): created, max %d threads, %s", (void *)this, _maxThreadCount,
                     lazy ? "lazy" : "eager");
        return;
}

ThreadPool::~ThreadPool() {
        promekiDebug("ThreadPool(%p): destroying, %d threads spawned", (void *)this, _threadCount);
        {
                Mutex::Locker locker(_mutex);
                _shutdown = true;
                _cv.wakeAll();
        }
        for (auto &t : _threads) {
                if (t.joinable()) t.join();
        }
        {
                Mutex::Locker locker(registryMutex());
                List<ThreadPool *> &reg = registry();
                for (size_t i = 0; i < reg.size(); i++) {
                        if (reg[i] == this) {
                                reg.remove(i);
                                break;
                        }
                }
        }
        promekiDebug("ThreadPool(%p): destroyed", (void *)this);
        return;
}

void ThreadPool::setNamePrefix(const String &prefix) {
        Mutex::Locker locker(_mutex);
        _namePrefix = prefix;
        promekiDebug("ThreadPool(%p): name prefix set to '%s'", (void *)this, prefix.cstr());
        return;
}

String ThreadPool::namePrefix() const {
        Mutex::Locker locker(_mutex);
        return _namePrefix;
}

void ThreadPool::setName(const String &name) {
        Mutex::Locker locker(_mutex);
        _name = name;
}

String ThreadPool::name() const {
        Mutex::Locker locker(_mutex);
        return _name;
}

void ThreadPool::setThreadCount(int count, bool lazy) {
        if (count < 0) count = 0;
        {
                Mutex::Locker locker(_mutex);
                if (count == _maxThreadCount) return;
                promekiDebug("ThreadPool(%p): resizing from %d to %d max threads, %s", (void *)this, _maxThreadCount,
                             count, lazy ? "lazy" : "eager");
                _shutdown = true;
                _cv.wakeAll();
        }
        for (auto &t : _threads) {
                if (t.joinable()) t.join();
        }
        {
                Mutex::Locker locker(_mutex);
                _threads.clear();
                _maxThreadCount = count;
                _threadCount = 0;
                _waitingCount = 0;
                _shutdown = false;
                if (!lazy && count > 0) spawnThreads(count);
        }
        return;
}

int ThreadPool::maxThreadCount() const {
        Mutex::Locker locker(_mutex);
        return _maxThreadCount;
}

int ThreadPool::threadCount() const {
        Mutex::Locker locker(_mutex);
        return _threadCount;
}

int ThreadPool::activeThreadCount() const {
        Mutex::Locker locker(_mutex);
        return _activeCount;
}

void ThreadPool::waitForDone() {
        _mutex.lock();
        auto pred = [this] {
                return _tasks.isEmpty() && _activeCount == 0;
        };
        _doneCv.wait(_mutex, pred);
        _mutex.unlock();
        return;
}

Error ThreadPool::waitForDone(unsigned int timeoutMs) {
        _mutex.lock();
        auto pred = [this] {
                return _tasks.isEmpty() && _activeCount == 0;
        };
        Error err = _doneCv.wait(_mutex, pred, timeoutMs);
        _mutex.unlock();
        return err;
}

void ThreadPool::clear() {
        Mutex::Locker locker(_mutex);
        _tasks.clear();
        return;
}

ThreadPool::WorkRecord *ThreadPool::recordFor(WorkTag tag) {
        const uint64_t id = tag.isValid() ? tag.id() : kUntaggedId;
        // Hot path: existing record under a read lock.
        {
                ReadWriteLock::ReadLocker rl(_statsLock);
                auto                      it = _stats.find(id);
                if (it != _stats.end()) return it->second.get();
        }
        // Cold path: install a new record under a write lock.
        ReadWriteLock::WriteLocker wl(_statsLock);
        auto                       it = _stats.find(id);
        if (it != _stats.end()) return it->second.get();
        auto rec = std::make_unique<WorkRecord>();
        rec->tag = tag;
        if (id == kUntaggedId) {
                rec->name = "(untagged)";
        } else {
                rec->name = tag.name();
                if (rec->name.isEmpty()) {
                        rec->name = String("id:") + String::number(static_cast<int64_t>(id));
                }
        }
        WorkRecord *raw = rec.get();
        _stats.insert(id, std::move(rec));
        return raw;
}

void ThreadPool::runTaskWithStats(TaggedTask &t) {
        const TimeStamp wall0 = TimeStamp::now();
        const Duration  queueWait = wall0 - t.enqueuedAt;
        const int64_t   cpu0 = threadCpuNs();
        if (t.callable) t.callable();
        const int64_t   cpu1 = threadCpuNs();
        const TimeStamp wall1 = TimeStamp::now();
        const int64_t   wallDeltaNs = (wall1 - wall0).nanoseconds();
        const int64_t   cpuDeltaNs = cpu1 - cpu0;
        WorkRecord     *rec = recordFor(t.tag);
        rec->totalWallNs.fetchAndAdd(wallDeltaNs);
        rec->totalCpuNs.fetchAndAdd(cpuDeltaNs);
        rec->totalQueueWaitNs.fetchAndAdd(queueWait.nanoseconds());
        rec->count.fetchAndAdd(1);
}

List<ThreadPool::WorkStats> ThreadPool::snapshotWorkStats() const {
        List<WorkStats>           out;
        ReadWriteLock::ReadLocker rl(_statsLock);
        for (const auto &entry : _stats) {
                const WorkRecord &rec = *entry.second;
                WorkStats         s;
                s.tag = rec.tag;
                s.name = rec.name;
                s.totalWall = Duration::fromNanoseconds(rec.totalWallNs.value());
                s.totalCpu = Duration::fromNanoseconds(rec.totalCpuNs.value());
                s.totalQueueWait = Duration::fromNanoseconds(rec.totalQueueWaitNs.value());
                s.count = rec.count.value();
                out.pushToBack(s);
        }
        std::sort(out.begin(), out.end(), [](const WorkStats &a, const WorkStats &b) {
                if (a.totalCpu.nanoseconds() != b.totalCpu.nanoseconds()) {
                        return a.totalCpu.nanoseconds() > b.totalCpu.nanoseconds();
                }
                return a.totalWall.nanoseconds() > b.totalWall.nanoseconds();
        });
        return out;
}

void ThreadPool::resetWorkStats() {
        ReadWriteLock::ReadLocker rl(_statsLock);
        for (const auto &entry : _stats) {
                WorkRecord &rec = *entry.second;
                rec.totalWallNs.setValue(0);
                rec.totalCpuNs.setValue(0);
                rec.totalQueueWaitNs.setValue(0);
                rec.count.setValue(0);
        }
}

void ThreadPool::workerFunc(int index) {
        {
                Mutex::Locker locker(_mutex);
                if (!_namePrefix.isEmpty()) {
                        String name = _namePrefix + String::number(index);
                        Thread::setCurrentThreadName(name);
                        promekiDebug("ThreadPool(%p): thread %d named '%s'", (void *)this, index, name.cstr());
                }
        }
        promekiDebug("ThreadPool(%p): thread %d started", (void *)this, index);
        for (;;) {
                TaggedTask task;
                {
                        _mutex.lock();
                        _waitingCount++;
                        while (_tasks.isEmpty() && !_shutdown) {
                                _cv.wait(_mutex);
                        }
                        _waitingCount--;
                        if (_shutdown && _tasks.isEmpty()) {
                                _mutex.unlock();
                                promekiDebug("ThreadPool(%p): thread %d exiting", (void *)this, index);
                                return;
                        }
                        task = std::move(_tasks.front());
                        _tasks.remove(static_cast<size_t>(0));
                        _activeCount++;
                        _mutex.unlock();
                }
                runTaskWithStats(task);
                {
                        Mutex::Locker locker(_mutex);
                        _activeCount--;
                        if (_tasks.isEmpty() && _activeCount == 0) {
                                _doneCv.wakeAll();
                        }
                }
        }
        return;
}

void ThreadPool::spawnThreads(int count) {
        for (int i = 0; i < count; i++) {
                int index = _threadCount;
                _threads.pushToBack(std::thread(&ThreadPool::workerFunc, this, index));
                _threadCount++;
        }
        return;
}

void ThreadPool::maybeSpawnOne() {
        if (_threadCount >= _maxThreadCount) return;
        if (_waitingCount > 0) return;
        promekiDebug("ThreadPool(%p): spawning thread %d (active %d, waiting %d, max %d)", (void *)this, _threadCount,
                     _activeCount, _waitingCount, _maxThreadCount);
        spawnThreads(1);
        return;
}

PROMEKI_NAMESPACE_END
