/**
 * @file      tsqueue.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <mutex>
#include <queue>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <promeki/namespace.h>

using namespace std::chrono_literals;

PROMEKI_NAMESPACE_BEGIN

// A thread-safe queue implementation
template <typename T>
class TSQueue {
        public:
                using Locker = std::unique_lock<std::mutex>;

                TSQueue() { };
                ~TSQueue() { };

                void push(const T &val) {
                        Locker locker(_mutex);
                        _queue.push(val);
                        _cv.notify_all();
                        return;
                }

                void push(const T &&val) {
                        Locker locker(_mutex);
                        _queue.push(std::move(val));
                        _cv.notify_all();
                        return;
                }

                void push(const std::vector<T> &list) {
                        Locker locker(_mutex);
                        for(const auto &item : list) _queue.push(item);
                        _cv.notify_all();
                        return;
                }

                T pop() {
                        Locker locker(_mutex);
                        _cv.wait(locker, [this] { return !_queue.empty(); });
                        T ret = _queue.front();
                        _queue.pop();
                        return ret;
                }

                bool popOrFail(T &val) {
                        Locker locker(_mutex);
                        if(_queue.empty()) return false;
                        val = _queue.front();
                        _queue.pop();
                        return true;
                }
 
                 T peek() {
                        Locker locker(_mutex);
                        _cv.wait(locker, [this] { return !_queue.empty(); });
                        T ret = _queue.front();
                        return ret;
                }

                bool peekOrFail(T &val) {
                        Locker locker(_mutex);
                        if(_queue.empty()) return false;
                        val = _queue.front();
                        return true;
                }

                void waitForEmpty() {
                        Locker locker(_mutex);
                        while(!_cv.wait_for(locker, 100ms, [this] { return _queue.empty(); }));
                        return;
                }

                size_t size() const {
                        Locker locker(_mutex);
                        return _queue.size();
                }

                void clear() {
                        Locker locker(_mutex);
                        std::queue<T> empty;
                        std::swap(_queue, empty);
                        return;
                }

        private:
                mutable std::mutex              _mutex;
                std::condition_variable         _cv;
                std::queue<T>                   _queue;
};

PROMEKI_NAMESPACE_END

