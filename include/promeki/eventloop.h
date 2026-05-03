/**
 * @file      eventloop.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <variant>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/queue.h>
#include <promeki/list.h>
#include <promeki/timestamp.h>
#include <promeki/event.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class ObjectBase;

// Defined in eventloop.cpp; owns the platform-specific wake file
// descriptor (eventfd on Linux, self-pipe elsewhere) used to
// interrupt poll() when new work is posted.
class EventLoopWakeFd;

/**
 * @brief Per-thread event loop providing event dispatch, timers, and posted callables.
 * @ingroup events
 *
 * EventLoop is not an ObjectBase — it is infrastructure that ObjectBase
 * instances attach to.  Each thread may have at most one active EventLoop,
 * tracked by a thread_local pointer accessible via current().
 *
 * Two execution models are supported:
 * - exec() — blocks, processes events until quit() is called.
 * - processEvents() — processes pending events and returns, suitable
 *   for WebAssembly or other environments where the host owns the run loop.
 *
 * exec() is simply a loop calling processEvents(WaitForMore).
 *
 * @par Thread Safety
 * Mixed.  @c postCallable, @c postEvent, @c quit, @c startTimer,
 * @c stopTimer, @c addIoSource, @c removeIoSource are fully
 * thread-safe — the whole point is to post work onto another
 * thread's loop.  @c exec / @c processEvents must run on the
 * EventLoop's owning thread (the thread on which it was
 * constructed).  @c current() returns the calling thread's loop.
 *
 * @par Example
 * @code
 * EventLoop loop;
 *
 * // Post work to this thread's event loop
 * loop.postCallable([]() {
 *     promekiInfo("Running on event loop thread");
 * });
 *
 * // Start a repeating 100ms timer
 * int timerId = loop.startTimer(100, []() {
 *     promekiInfo("Timer fired");
 * });
 *
 * // Run the event loop (blocks until quit)
 * int exitCode = loop.exec();
 * @endcode
 * @note Timer precision is millisecond-level, based on steady_clock polling.
 */
class EventLoop {
        public:
                /** @brief Flags controlling processEvents() behavior. */
                enum ProcessEventsFlag : uint32_t {
                        ExcludeTimers = 0x01, ///< Skip timer processing.
                        ExcludePosted = 0x02, ///< Skip posted callables and events.
                        WaitForMore = 0x04    ///< Block until at least one event is available.
                };

                /**
                 * @brief Returns the EventLoop running on the current thread.
                 * @return The current thread's EventLoop, or nullptr if none.
                 */
                static EventLoop *current();

                /**
                 * @brief Constructs an EventLoop and registers it as current for this thread.
                 *
                 * Only one EventLoop may be active per thread.  If one already
                 * exists, the new one replaces it (the previous pointer is not
                 * restored on destruction).
                 */
                EventLoop();

                /**
                 * @brief Destroys the EventLoop.
                 *
                 * Drains any remaining queued items, freeing owned Event
                 * pointers.  Clears the thread-local pointer if it still
                 * points to this instance.
                 */
                ~EventLoop();

                EventLoop(const EventLoop &) = delete;
                EventLoop &operator=(const EventLoop &) = delete;

                /**
                 * @brief Blocks and processes events until quit() is called.
                 * @return The exit code passed to quit().
                 */
                int exec();

                /**
                 * @brief Processes pending events and returns.
                 *
                 * @param flags    Combination of ProcessEventsFlag values.
                 * @param timeoutMs When WaitForMore is set, the maximum time to
                 *        wait for events in milliseconds.  Zero with WaitForMore
                 *        waits indefinitely.  Zero without WaitForMore drains
                 *        pending events and returns immediately.
                 */
                void processEvents(uint32_t flags = 0, unsigned int timeoutMs = 0);

                /**
                 * @brief Signals the event loop to exit with the given return code.
                 *
                 * Thread-safe.  The loop will exit after the current
                 * processEvents() iteration completes.
                 *
                 * @param returnCode The exit code returned by exec().
                 */
                void quit(int returnCode = 0);

                /**
                 * @brief Posts a callable to be executed on this EventLoop's thread.
                 *
                 * Thread-safe.  The callable will be invoked during the next
                 * processEvents() call.
                 *
                 * @param func The callable to execute.
                 */
                void postCallable(std::function<void()> func);

                /**
                 * @brief Posts an Event to this EventLoop for delivery to a receiver.
                 *
                 * Takes ownership of @p event.  Thread-safe.  The event will be
                 * delivered during the next processEvents() call via the
                 * receiver's event() virtual method.
                 *
                 * @param receiver The ObjectBase to deliver the event to.
                 * @param event    The event to deliver.  Ownership is transferred.
                 */
                void postEvent(ObjectBase *receiver, Event *event);

                /**
                 * @brief Starts a timer that delivers TimerEvent to an ObjectBase.
                 *
                 * The receiver's timerEvent() virtual method will be called
                 * each time the timer fires.  Thread-safe: may be called
                 * from any thread.  When called from a thread other than
                 * the one running @c exec(), the worker loop is woken
                 * so it re-evaluates @c nextTimerTimeout immediately.
                 *
                 * @param receiver   The ObjectBase to receive TimerEvents.
                 * @param intervalMs The timer interval in milliseconds.
                 * @param singleShot If true, the timer fires once and is removed.
                 * @return The timer ID, usable with stopTimer().
                 */
                int startTimer(ObjectBase *receiver, unsigned int intervalMs, bool singleShot = false);

                /**
                 * @brief Starts a standalone callable timer.
                 *
                 * No ObjectBase is needed — the callable runs directly on the
                 * EventLoop's thread each time the timer fires.  Thread-safe:
                 * see the ObjectBase overload for cross-thread semantics.
                 *
                 * @param intervalMs The timer interval in milliseconds.
                 * @param func       The callable to invoke when the timer fires.
                 * @param singleShot If true, the timer fires once and is removed.
                 * @return The timer ID, usable with stopTimer().
                 */
                int startTimer(unsigned int intervalMs, std::function<void()> func, bool singleShot = false);

                /**
                 * @brief Stops and removes a timer.
                 *
                 * Thread-safe: may be called from any thread.  When called
                 * from a thread other than the one running @c exec(), a
                 * no-op wake marker is pushed so the worker loop notices
                 * the change.  A stopTimer() call issued from another
                 * thread can race against a timer callback that is already
                 * executing — the callback will finish running before the
                 * stop takes effect, so any resources the callback depends
                 * on must outlive that small window.
                 *
                 * @param timerId The ID returned by startTimer().
                 */
                void stopTimer(int timerId);

                /**
                 * @brief Returns whether the event loop is currently running.
                 * @return @c true if exec() is active.
                 */
                bool isRunning() const { return _running.value(); }

                /**
                 * @brief Returns the exit code set by the most recent quit().
                 * @return The exit code, or 0 if quit() was never called.
                 */
                int exitCode() const { return _exitCode.value(); }

                /**
                 * @brief Returns milliseconds until the next timer fires.
                 *
                 * Intended for external pumps that want to bound their
                 * own wait (e.g. @c SDL_WaitEventTimeout) by the
                 * loop's next timer deadline so armed timers fire on
                 * schedule even when no external events are arriving.
                 *
                 * @return The milliseconds remaining until the next
                 *         timer fire (minimum 1 for sub-millisecond
                 *         remaining), or @c 0 when no timer is armed.
                 */
                unsigned int nextTimerTimeout() const;

                /**
                 * @name I/O event bitmask constants
                 *
                 * Bitmask values for @ref addIoSource and the @ref IoCallback
                 * @c events parameter.  Defined as @c static @c constexpr
                 * @c uint32_t (rather than as an unscoped @c enum) so the
                 * public API takes a plain @c uint32_t mask without an
                 * implicit integer-conversion footgun.
                 * @{
                 */
                static constexpr uint32_t IoRead = 0x01;  ///< fd is readable
                static constexpr uint32_t IoWrite = 0x02; ///< fd is writable
                static constexpr uint32_t IoError = 0x04; ///< error / hangup (always reported; mask is informational)
                /** @} */

                /**
                 * @brief Callback invoked when a registered I/O source becomes ready.
                 *
                 * The @p events bitmask reports which of @ref IoRead /
                 * @ref IoWrite / @ref IoError actually fired for this
                 * wake.  Always a subset (or equal to) the events the
                 * source was registered with, plus @ref IoError which
                 * is reported whenever @c POLLERR / @c POLLHUP surface
                 * regardless of the caller's mask.
                 */
                using IoCallback = std::function<void(int fd, uint32_t events)>;

                /**
                 * @brief Registers a file descriptor to be monitored by this EventLoop.
                 *
                 * While registered, @p cb is invoked on the EventLoop's
                 * thread each time @p fd becomes ready for any of the
                 * requested @p events.  The callback runs synchronously
                 * between @c processEvents iterations (not from inside
                 * @c poll), so it may call any EventLoop API including
                 * @ref removeIoSource on itself.
                 *
                 * Thread-safe: may be called from any thread.  When
                 * called cross-thread, the EventLoop is woken so its
                 * next poll includes the newly registered fd.
                 *
                 * The caller retains ownership of @p fd — the EventLoop
                 * never closes it.  Close the fd only after
                 * @ref removeIoSource has been called and the handle
                 * is no longer in use.
                 *
                 * The API intentionally uses an opaque integer handle
                 * rather than an ObjectBase-receiver pattern: TUI and
                 * SDL register a handful of long-lived sources at
                 * startup and unregister them at shutdown, which maps
                 * naturally onto a return-the-handle-and-hold-it model.
                 * An ObjectBase-receiver variant can be layered on top
                 * later if a widget-like consumer emerges — don't add
                 * one speculatively.
                 *
                 * @param fd     The file descriptor to monitor.
                 * @param events Bitmask of @c IoEvent values requesting
                 *               read / write readiness notifications.
                 * @param cb     Callback invoked with (fd, readyEvents)
                 *               on each readiness event.
                 * @return A handle @c >= 0 on success, or @c -1 on
                 *         failure (invalid @p fd, empty @p cb, empty
                 *         @p events, or unsupported on this platform).
                 *
                 * @note On non-POSIX platforms (Windows, Emscripten)
                 *       this call currently returns @c -1 and records
                 *       @c Error::NotImplemented in the library log.
                 *       Emscripten cooperative loops should move to
                 *       explicit non-blocking @ref processEvents calls.
                 */
                int addIoSource(int fd, uint32_t events, IoCallback cb);

                /**
                 * @brief Removes a previously-registered I/O source.
                 *
                 * Thread-safe.  If called cross-thread while a callback
                 * is in flight on the loop thread, the in-flight
                 * callback completes first; subsequent readiness events
                 * for this handle do not fire.  Unknown handles are
                 * silently ignored.
                 *
                 * @param handle The handle returned by @ref addIoSource.
                 */
                void removeIoSource(int handle);

        private:
                struct CallableItem {
                                std::function<void()> func;
                };
                struct EventItem {
                                ObjectBase *receiver;
                                Event      *event;
                };
                struct QuitItem {
                                int code;
                };
                using Item = std::variant<CallableItem, EventItem, QuitItem>;

                struct TimerInfo {
                                int                   id;
                                ObjectBase           *receiver; ///< nullptr for callable timers
                                std::function<void()> func;     ///< For callable timers
                                unsigned int          intervalMs;
                                bool                  singleShot;
                                TimeStamp             nextFire;
                };

                static thread_local EventLoop *_current;

                Queue<Item>  _queue;
                Atomic<bool> _running;
                Atomic<int>  _exitCode;

                // Timer list access is guarded by _timersMutex.  Any
                // thread may install or stop timers via startTimer /
                // stopTimer, so the mutex is acquired on every touch.
                // processTimers() takes a snapshot of the ready-to-fire
                // entries under the lock, releases the lock, and then
                // invokes callbacks — this avoids deadlocks if a timer
                // callback calls startTimer() or stopTimer() on the
                // same event loop, and keeps the lock hold time bounded.
                mutable Mutex   _timersMutex;
                List<TimerInfo> _timers;
                Atomic<int>     _nextTimerId{1};

                // Platform wake fd (eventfd on Linux, self-pipe
                // elsewhere).  Owned by the EventLoop; opened in the
                // constructor, closed in the destructor.  Written by
                // postCallable / postEvent / quit / startTimer /
                // stopTimer to unblock poll() in waitOnSources, and
                // included in every poll set as index 0.
                using WakeFdUPtr = UniquePtr<EventLoopWakeFd>;
                WakeFdUPtr _wake;

                // I/O source registration.  Mutation under _ioMutex,
                // poll set built under _ioMutex into a short-lived
                // stack copy, callbacks fired after the lock is
                // released so a callback may call addIoSource /
                // removeIoSource on the same EventLoop without
                // deadlocking.
                struct IoSource {
                                int        handle;
                                int        fd;
                                uint32_t   events;
                                IoCallback cb;
                                bool       pendingRemove = false;
                };
                mutable Mutex  _ioMutex;
                List<IoSource> _ioSources;
                Atomic<int>    _nextIoHandle{1};

                /**
                 * @brief Writes to the internal wake fd unconditionally.
                 *
                 * Cheap: one @c write(wakeFd, ...) call.  Safe to call
                 * from any thread.  Redundant wakes are harmless —
                 * the fd is drained inside @c waitOnSources before
                 * the queue is processed, and a spurious wake just
                 * costs one extra @c poll() return.
                 */
                void wakeSelf();

                bool dispatchItem(Item &item);
                void processTimers();

                // Waits on the wake fd + registered I/O sources via
                // poll(), for up to @p waitMs milliseconds (0 = wait
                // indefinitely — callers clamp by timers before
                // calling).  On return, drains the wake fd and fires
                // any ready I/O source callbacks.  On non-POSIX
                // platforms, falls back to the condvar-based
                // Queue::pop wait so Windows / Emscripten builds
                // still compile and behave as before (minus
                // IoSource support).
                void waitOnSources(unsigned int waitMs);
};

PROMEKI_NAMESPACE_END
