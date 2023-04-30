/*****************************************************************************
 * tsqueue.h
 * April 28, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <mutex>
#include <queue>
#include <condition_variable>

namespace promeki {

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
                        _cv.notify_one();
                        return;
                }

                void push(const T &&val) {
                        Locker locker(_mutex);
                        _queue.push(std::move(val));
                        _cv.notify_one();
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

} // namespace promeki
