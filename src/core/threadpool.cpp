/**
 * @file      threadpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/threadpool.h>
#include <promeki/thread.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(ThreadPool)

ThreadPool::ThreadPool(int maxThreadCount, bool lazy) {
        int count = maxThreadCount;
        if (count < 0) {
                count = static_cast<int>(std::thread::hardware_concurrency());
                if (count <= 0) count = 1;
        }
        _maxThreadCount = count;
        if (!lazy && count > 0) spawnThreads(count);
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
                Task task;
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
                task();
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
