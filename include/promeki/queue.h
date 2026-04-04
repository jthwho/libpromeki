/**
 * @file      queue.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <queue>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Thread-safe FIFO queue.
 * @ingroup containers
 *
 * All public methods are safe to call concurrently from multiple threads.
 *
 * @par Example
 * @code
 * Queue<String> q;
 * q.emplace("first");
 * q.emplace("second");
 * String val = q.dequeue();  // "first"
 * bool empty = q.isEmpty();  // false
 * @endcode
 * Internally synchronized with a Mutex and WaitCondition.  Blocking
 * methods (pop(), peek(), waitForEmpty()) will sleep until their condition
 * is met rather than spinning or polling.
 *
 * @note waitForEmpty() indicates that all items have been removed from the
 * queue, not that they have been fully processed by the consumer.  If you
 * need a "processing complete" guarantee, use an out-of-band mechanism
 * such as a promise/future handshake.
 *
 * @tparam T The element type stored in the queue.
 */
template <typename T>
class Queue {
        public:
                Queue() { };
                ~Queue() { };

                /**
                 * @brief Pushes a copy of @p val onto the back of the queue.
                 * @param val Value to enqueue.
                 */
                void push(const T &val) {
                        Mutex::Locker locker(_mutex);
                        _queue.push(val);
                        _cv.wakeOne();
                        return;
                }

                /**
                 * @brief Moves @p val onto the back of the queue.
                 * @param val Rvalue reference to enqueue.
                 */
                void push(T &&val) {
                        Mutex::Locker locker(_mutex);
                        _queue.push(std::move(val));
                        _cv.wakeOne();
                        return;
                }

                /**
                 * @brief Constructs an element in-place at the back of the queue.
                 * @tparam Args Constructor argument types.
                 * @param args Arguments forwarded to the T constructor.
                 */
                template <typename... Args>
                void emplace(Args&&... args) {
                        Mutex::Locker locker(_mutex);
                        _queue.emplace(std::forward<Args>(args)...);
                        _cv.wakeOne();
                        return;
                }

                /**
                 * @brief Pushes every element in @p list onto the back of the queue.
                 * @param list List of values to enqueue.
                 */
                void push(const List<T> &list) {
                        Mutex::Locker locker(_mutex);
                        for(const auto &item : list) _queue.push(item);
                        _cv.wakeAll();
                        return;
                }

                /**
                 * @brief Removes and returns the front element, blocking until
                 *        one is available or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return A pair of the dequeued element (default-constructed on
                 *         timeout) and Error::Ok or Error::Timeout.
                 */
                std::pair<T, Error> pop(unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error err = _cv.wait(_mutex,
                                [this] { return !_queue.empty(); }, timeoutMs);
                        if(err != Error::Ok) return {T{}, err};
                        T ret = std::move(_queue.front());
                        _queue.pop();
                        if(_queue.empty()) _cv.wakeAll();
                        return {std::move(ret), Error::Ok};
                }

                /**
                 * @brief Tries to pop the front element without blocking.
                 * @param[out] val Receives the dequeued element on success.
                 * @return @c true if an element was dequeued, @c false if the queue was empty.
                 */
                bool popOrFail(T &val) {
                        Mutex::Locker locker(_mutex);
                        if(_queue.empty()) return false;
                        val = std::move(_queue.front());
                        _queue.pop();
                        if(_queue.empty()) _cv.wakeAll();
                        return true;
                }

                /**
                 * @brief Returns a copy of the front element without removing it,
                 *        blocking until one is available or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return A pair of the front element (default-constructed on
                 *         timeout) and Error::Ok or Error::Timeout.
                 */
                std::pair<T, Error> peek(unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error err = _cv.wait(_mutex,
                                [this] { return !_queue.empty(); }, timeoutMs);
                        if(err != Error::Ok) return {T{}, err};
                        T ret = _queue.front();
                        return {std::move(ret), Error::Ok};
                }

                /**
                 * @brief Tries to copy the front element without blocking or removing it.
                 * @param[out] val Receives a copy of the front element on success.
                 * @return @c true if an element was available, @c false if the queue was empty.
                 */
                bool peekOrFail(T &val) {
                        Mutex::Locker locker(_mutex);
                        if(_queue.empty()) return false;
                        val = _queue.front();
                        return true;
                }

                /**
                 * @brief Blocks until the queue is empty.
                 *
                 * Useful for backpressure: a producer can wait until the consumer has
                 * drained the queue before pushing more work.  Note that this only
                 * guarantees the items have been popped, not that the consumer has
                 * finished processing them.
                 *
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value of
                 *        zero (the default) waits indefinitely.
                 * @return Error::Ok if the queue drained, Error::Timeout if the
                 *         timeout elapsed first.
                 */
                Error waitForEmpty(unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        return _cv.wait(_mutex,
                                [this] { return _queue.empty(); }, timeoutMs);
                }

                /**
                 * @brief Returns true if the queue has no elements.
                 * @return True if empty.
                 */
                bool isEmpty() const {
                        Mutex::Locker locker(_mutex);
                        return _queue.empty();
                }

                /**
                 * @brief Returns the number of elements currently in the queue.
                 * @return Current queue depth.
                 */
                size_t size() const {
                        Mutex::Locker locker(_mutex);
                        return _queue.size();
                }

                /**
                 * @brief Removes all elements from the queue.
                 */
                void clear() {
                        Mutex::Locker locker(_mutex);
                        std::queue<T> empty;
                        std::swap(_queue, empty);
                        return;
                }

        private:
                mutable Mutex           _mutex;
                WaitCondition           _cv;
                std::queue<T>           _queue;
};

PROMEKI_NAMESPACE_END
