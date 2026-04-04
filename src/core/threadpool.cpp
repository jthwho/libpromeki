/**
 * @file      threadpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/threadpool.h>

PROMEKI_NAMESPACE_BEGIN

ThreadPool::ThreadPool(int threadCount) {
        int count = threadCount;
        if(count < 0) {
                count = static_cast<int>(std::thread::hardware_concurrency());
                if(count <= 0) count = 1;
        }
        if(count > 0) spawnThreads(count);
        return;
}

ThreadPool::~ThreadPool() {
        {
                Mutex::Locker locker(_mutex);
                _shutdown = true;
                _cv.wakeAll();
        }
        for(auto &t : _threads) {
                if(t.joinable()) t.join();
        }
        return;
}

void ThreadPool::setThreadCount(int count) {
        if(count < 0) count = 0;
        {
                Mutex::Locker locker(_mutex);
                if(count == _threadCount) return;
                _shutdown = true;
                _cv.wakeAll();
        }
        for(auto &t : _threads) {
                if(t.joinable()) t.join();
        }
        {
                Mutex::Locker locker(_mutex);
                _threads.clear();
                _threadCount = 0;
                _shutdown = false;
                if(count > 0) spawnThreads(count);
        }
        return;
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
        auto pred = [this] { return _tasks.isEmpty() && _activeCount == 0; };
        _doneCv.wait(_mutex, pred);
        _mutex.unlock();
        return;
}

Error ThreadPool::waitForDone(unsigned int timeoutMs) {
        _mutex.lock();
        auto pred = [this] { return _tasks.isEmpty() && _activeCount == 0; };
        Error err = _doneCv.wait(_mutex, pred, timeoutMs);
        _mutex.unlock();
        return err;
}

void ThreadPool::clear() {
        Mutex::Locker locker(_mutex);
        _tasks.clear();
        return;
}

void ThreadPool::workerFunc() {
        for(;;) {
                Task task;
                {
                        _mutex.lock();
                        while(_tasks.isEmpty() && !_shutdown) {
                                _cv.wait(_mutex);
                        }
                        if(_shutdown && _tasks.isEmpty()) {
                                _mutex.unlock();
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
                        if(_tasks.isEmpty() && _activeCount == 0) {
                                _doneCv.wakeAll();
                        }
                }
        }
        return;
}

void ThreadPool::spawnThreads(int count) {
        for(int i = 0; i < count; i++) {
                _threads.pushToBack(std::thread(&ThreadPool::workerFunc, this));
                _threadCount++;
        }
        return;
}

PROMEKI_NAMESPACE_END
