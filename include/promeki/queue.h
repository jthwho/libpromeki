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
#include <promeki/result.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Thread-safe FIFO queue.
 * @ingroup containers
 *
 * All public methods are safe to call concurrently from multiple threads.
 * Internally synchronized with a Mutex and WaitCondition.  Blocking
 * methods (pop(), peek(), waitForEmpty()) will sleep until their condition
 * is met rather than spinning or polling.
 *
 * @par Optional bounding
 * @ref setMaxSize installs a soft cap on the queue depth; once the cap
 * is reached subsequent @c push / @c emplace calls block until a
 * consumer pops, the supplied timeout expires, or @ref cancelWaiters
 * is invoked.  A cap of @c 0 (the default) preserves the unbounded
 * behaviour and disables the producer-side wait entirely — existing
 * call sites are unaffected.
 *
 * @par Shutdown
 * @ref cancelWaiters wakes every blocked producer and consumer and
 * causes their outstanding @c pop / @c push call to return
 * @c Error::Cancelled.  Used by long-running pipelines that need to
 * tear queues down promptly without inventing sentinel values.  The
 * cancel state latches — once set it stays set until @ref reset is
 * called, so any future blocking call returns @c Error::Cancelled
 * immediately.
 *
 * @note waitForEmpty() indicates that all items have been removed from the
 * queue, not that they have been fully processed by the consumer.  If you
 * need a "processing complete" guarantee, use an out-of-band mechanism
 * such as a promise/future handshake.
 *
 * @tparam T The element type stored in the queue.
 *
 * @par Example
 * @code
 * Queue<String> q;
 * q.emplace("first");
 * q.emplace("second");
 * String val = q.dequeue();  // "first"
 * bool empty = q.isEmpty();  // false
 * @endcode
 */
template <typename T> class Queue {
        public:
                Queue() = default;
                ~Queue() = default;

                /**
                 * @brief Configures a soft cap on queue depth.
                 *
                 * After this call, blocking @c push / @c emplace
                 * overloads sleep when @ref size has reached @p n
                 * until a consumer pops or the cancel state is set.
                 * Non-blocking @c push / @c emplace behave as before
                 * regardless of the cap (they cannot fail; callers
                 * that want backpressure must use the blocking
                 * overload).
                 *
                 * @param n The maximum depth, or @c 0 to disable
                 *          bounding (default).  Reducing the cap
                 *          below the current depth does not drop
                 *          existing items but blocks subsequent
                 *          pushes until the queue drains.
                 */
                void setMaxSize(size_t n) {
                        Mutex::Locker locker(_mutex);
                        _maxSize = n;
                        // A larger / disabled cap may have just made room for
                        // producers waiting on a smaller cap that was set
                        // earlier.  No-op when no producer is parked.
                        _notFull.wakeAll();
                }

                /**
                 * @brief Returns the configured soft cap, or @c 0 if
                 *        the queue is unbounded.
                 */
                size_t maxSize() const {
                        Mutex::Locker locker(_mutex);
                        return _maxSize;
                }

                /**
                 * @brief Pushes a copy of @p val onto the back of the queue.
                 *
                 * Non-blocking — even when a max size has been
                 * configured, this overload ignores the cap and will
                 * always insert.  Callers that want backpressure must
                 * use the @ref pushBlocking overload.
                 *
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
                template <typename... Args> void emplace(Args &&...args) {
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
                        for (const auto &item : list) _queue.push(item);
                        _cv.wakeAll();
                        return;
                }

                /**
                 * @brief Blocking @c push that respects @ref setMaxSize.
                 *
                 * If a max size has been configured and the queue is
                 * full, blocks until either a slot frees up, the
                 * timeout expires, or the queue is cancelled via
                 * @ref cancelWaiters.  When no max size has been set
                 * the call inserts immediately and returns
                 * @c Error::Ok.
                 *
                 * @param val       Value to enqueue (copied).
                 * @param timeoutMs Maximum time to wait in
                 *                  milliseconds.  A value of @c 0
                 *                  (the default) waits indefinitely.
                 * @return @c Error::Ok on success, @c Error::Timeout
                 *         when the wait expired, or
                 *         @c Error::Cancelled when @ref cancelWaiters
                 *         fired.
                 */
                Error pushBlocking(const T &val, unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error err = waitForCapacity(timeoutMs);
                        if (err != Error::Ok) return err;
                        _queue.push(val);
                        _cv.wakeOne();
                        return Error::Ok;
                }

                /**
                 * @brief Move-form blocking push.  See @ref pushBlocking.
                 */
                Error pushBlocking(T &&val, unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error err = waitForCapacity(timeoutMs);
                        if (err != Error::Ok) return err;
                        _queue.push(std::move(val));
                        _cv.wakeOne();
                        return Error::Ok;
                }

                /**
                 * @brief Blocking in-place construction.  See @ref pushBlocking.
                 */
                template <typename... Args> Error emplaceBlocking(unsigned int timeoutMs, Args &&...args) {
                        Mutex::Locker locker(_mutex);
                        Error err = waitForCapacity(timeoutMs);
                        if (err != Error::Ok) return err;
                        _queue.emplace(std::forward<Args>(args)...);
                        _cv.wakeOne();
                        return Error::Ok;
                }

                /**
                 * @brief Removes and returns the front element, blocking until
                 *        one is available or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return A @ref Result holding the dequeued element on success,
                 *         a default-constructed value with @c Error::Timeout on
                 *         timeout, or @c Error::Cancelled if @ref cancelWaiters
                 *         was invoked while waiting (or before the call).
                 */
                Result<T> pop(unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error         err = _cv.wait(_mutex,
                                                     [this] { return _cancelled || !_queue.empty(); },
                                                     timeoutMs);
                        if (err != Error::Ok) return Result<T>(T{}, err);
                        if (_cancelled && _queue.empty()) return Result<T>(T{}, Error::Cancelled);
                        T ret = std::move(_queue.front());
                        _queue.pop();
                        if (_queue.empty()) _cv.wakeAll();
                        // A consumer popping always frees a slot for any
                        // producer parked on the not-full condition.
                        _notFull.wakeOne();
                        return makeResult(std::move(ret));
                }

                /**
                 * @brief Tries to pop the front element without blocking.
                 * @return A @ref Result holding the dequeued element on success,
                 *         or a default-constructed value with @c Error::Empty
                 *         if the queue had no elements.
                 */
                Result<T> tryPop() {
                        Mutex::Locker locker(_mutex);
                        if (_queue.empty()) return Result<T>(T{}, Error::Empty);
                        T ret = std::move(_queue.front());
                        _queue.pop();
                        if (_queue.empty()) _cv.wakeAll();
                        _notFull.wakeOne();
                        return makeResult(std::move(ret));
                }

                /**
                 * @brief Returns a copy of the front element without removing it,
                 *        blocking until one is available or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return A @ref Result holding a copy of the front element on
                 *         success, or a default-constructed value with
                 *         @c Error::Timeout / @c Error::Cancelled.
                 */
                Result<T> peek(unsigned int timeoutMs = 0) {
                        Mutex::Locker locker(_mutex);
                        Error         err = _cv.wait(_mutex,
                                                     [this] { return _cancelled || !_queue.empty(); },
                                                     timeoutMs);
                        if (err != Error::Ok) return Result<T>(T{}, err);
                        if (_cancelled && _queue.empty()) return Result<T>(T{}, Error::Cancelled);
                        return makeResult(T(_queue.front()));
                }

                /**
                 * @brief Tries to copy the front element without blocking or removing it.
                 * @return A @ref Result holding a copy of the front element on
                 *         success, or a default-constructed value with
                 *         @c Error::Empty if the queue was empty.
                 */
                Result<T> tryPeek() {
                        Mutex::Locker locker(_mutex);
                        if (_queue.empty()) return Result<T>(T{}, Error::Empty);
                        return makeResult(T(_queue.front()));
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
                        return _cv.wait(_mutex, [this] { return _queue.empty() || _cancelled; }, timeoutMs);
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
                 *
                 * Wakes any producer parked on a not-full condition
                 * — emptying the queue is functionally equivalent to
                 * a string of pops, so the producer wake is the same
                 * shape as the per-pop wake.
                 */
                void clear() {
                        Mutex::Locker locker(_mutex);
                        std::queue<T> empty;
                        std::swap(_queue, empty);
                        _notFull.wakeAll();
                        return;
                }

                /**
                 * @brief Wakes every blocked producer and consumer
                 *        with @c Error::Cancelled.
                 *
                 * Latches the cancel state — any subsequent blocking
                 * @c pop / @c push / @c peek call returns
                 * @c Error::Cancelled immediately without waiting.
                 * Existing items already in the queue can still be
                 * drained via @c tryPop until the queue is empty,
                 * after which the blocking @c pop variant returns
                 * @c Error::Cancelled.  Use @ref reset to lift the
                 * cancel and re-arm the queue.
                 *
                 * @par Rationale
                 * Sentinel pushes ("magic value means stop") work for
                 * single-consumer queues but become awkward when
                 * multiple consumers race for the cancel and need to
                 * see it exactly once each.  An out-of-band cancel
                 * latch covers both N producers and N consumers
                 * uniformly and lets the queue stay strongly typed
                 * (no need for a wrapper or sentinel-bearing variant).
                 */
                void cancelWaiters() {
                        Mutex::Locker locker(_mutex);
                        _cancelled = true;
                        _cv.wakeAll();
                        _notFull.wakeAll();
                }

                /**
                 * @brief Lifts the cancel state set by
                 *        @ref cancelWaiters and re-arms the queue.
                 *
                 * Existing items remain in place; only the cancel
                 * latch is cleared.  Useful when the same Queue
                 * instance is reused across multiple
                 * start / stop cycles.
                 */
                void reset() {
                        Mutex::Locker locker(_mutex);
                        _cancelled = false;
                }

                /**
                 * @brief Returns @c true if @ref cancelWaiters has been
                 *        called and @ref reset has not subsequently
                 *        cleared the latch.
                 */
                bool isCancelled() const {
                        Mutex::Locker locker(_mutex);
                        return _cancelled;
                }

        private:
                /**
                 * @brief Internal helper: wait until the queue has
                 *        room (or no cap has been configured).  Must
                 *        be called with @c _mutex held.
                 *
                 * @return @c Error::Ok when there is room (or the cap
                 *         is disabled), @c Error::Timeout when the
                 *         supplied timeout expired, @c Error::Cancelled
                 *         if the queue is in the cancelled state.
                 */
                Error waitForCapacity(unsigned int timeoutMs) {
                        if (_cancelled) return Error::Cancelled;
                        if (_maxSize == 0) return Error::Ok;
                        // Re-check after each wake: a consumer may have
                        // popped one slot and woken multiple parked
                        // producers, so any one of them must verify the
                        // cap before proceeding.
                        Error err = _notFull.wait(_mutex,
                                                  [this] { return _cancelled || _maxSize == 0 || _queue.size() < _maxSize; },
                                                  timeoutMs);
                        if (err != Error::Ok) return err;
                        if (_cancelled) return Error::Cancelled;
                        return Error::Ok;
                }

                mutable Mutex _mutex;
                WaitCondition _cv;
                WaitCondition _notFull;
                std::queue<T> _queue;
                size_t        _maxSize = 0;
                bool          _cancelled = false;
};

PROMEKI_NAMESPACE_END
