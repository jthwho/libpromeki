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

PROMEKI_NAMESPACE_BEGIN

class ObjectBase;

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
                        ExcludeTimers       = 0x01,  ///< Skip timer processing.
                        ExcludePosted       = 0x02,  ///< Skip posted callables and events.
                        WaitForMore         = 0x04   ///< Block until at least one event is available.
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
                 * @brief Sets a wake callback invoked whenever new work is posted.
                 *
                 * The callback is invoked from inside @c postCallable,
                 * @c postEvent, and @c quit after the item has been
                 * pushed to the queue.  It is intended for use by an
                 * external blocking mechanism (for example, the SDL
                 * event pump blocked on @c SDL_WaitEvent) that must be
                 * woken so it can return and let the owning event loop
                 * drain its queue.
                 *
                 * The callback runs on the posting thread, which may be
                 * any thread, so it must be thread-safe.  Only one wake
                 * callback may be registered at a time; passing an
                 * empty function removes the current callback.
                 *
                 * @param cb The wake callback, or an empty function to clear.
                 */
                void setWakeCallback(std::function<void()> cb);

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
                 * each time the timer fires.  Must be called from the
                 * EventLoop's thread.
                 *
                 * @param receiver   The ObjectBase to receive TimerEvents.
                 * @param intervalMs The timer interval in milliseconds.
                 * @param singleShot If true, the timer fires once and is removed.
                 * @return The timer ID, usable with stopTimer().
                 */
                int startTimer(ObjectBase *receiver, unsigned int intervalMs,
                               bool singleShot = false);

                /**
                 * @brief Starts a standalone callable timer.
                 *
                 * No ObjectBase is needed — the callable runs directly on the
                 * EventLoop's thread each time the timer fires.
                 *
                 * @param intervalMs The timer interval in milliseconds.
                 * @param func       The callable to invoke when the timer fires.
                 * @param singleShot If true, the timer fires once and is removed.
                 * @return The timer ID, usable with stopTimer().
                 */
                int startTimer(unsigned int intervalMs,
                               std::function<void()> func,
                               bool singleShot = false);

                /**
                 * @brief Stops and removes a timer.
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

        private:
                struct CallableItem { std::function<void()> func; };
                struct EventItem { ObjectBase *receiver; Event *event; };
                struct QuitItem { int code; };
                using Item = std::variant<CallableItem, EventItem, QuitItem>;

                struct TimerInfo {
                        int                     id;
                        ObjectBase              *receiver;      ///< nullptr for callable timers
                        std::function<void()>   func;           ///< For callable timers
                        unsigned int            intervalMs;
                        bool                    singleShot;
                        TimeStamp               nextFire;
                };

                static thread_local EventLoop   *_current;

                Queue<Item>                     _queue;
                Atomic<bool>                    _running;
                Atomic<int>                     _exitCode;
                List<TimerInfo>                 _timers;
                Atomic<int>                     _nextTimerId{1};

                // Optional wake callback, invoked after any post.  The
                // common use case (SDLApplication) sets this once at
                // startup and never changes it, so a plain function
                // with a mutex around mutation is sufficient; the
                // read/invoke path happens on every post but the
                // mutex is uncontended in practice.
                mutable Mutex                   _wakeMutex;
                std::function<void()>           _wakeCallback;

                void notifyWake();

                bool dispatchItem(Item &item);
                void processTimers();
                unsigned int nextTimerTimeout() const;
};

PROMEKI_NAMESPACE_END
