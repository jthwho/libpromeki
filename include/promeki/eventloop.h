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
#include <promeki/duration.h>
#include <promeki/hashmap.h>
#include <promeki/mutex.h>
#include <promeki/queue.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringregistry.h>
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
                 * @brief Optional integer-ID tag for a posted callable.
                 *
                 * Items minted from a string via the
                 * @c StringRegistry probe path; the same string
                 * across translation units yields the same id.
                 * Default-constructed (invalid) Items are
                 * treated as "unlabeled" by the labeled
                 * @ref postCallable overload — equivalent to
                 * the unlabeled overload.
                 *
                 * Used to partition the @ref Report::callables
                 * bucket into per-call-site sub-buckets so the
                 * monitor can identify which posters dominate
                 * a loop's wallclock.
                 */
                using Label = StringRegistry<"EventLoopLabel">::Item;

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
                 * @brief Posts a tagged callable for per-label bucketing in the activity monitor.
                 *
                 * Equivalent to the unlabeled overload except that
                 * the callable's wallclock contribution is also
                 * recorded in @ref Report::callablesByLabel keyed
                 * on @c label.id().  An invalid (default-constructed)
                 * @p label is treated as "unlabeled" — the call
                 * behaves exactly like the unlabeled overload.
                 *
                 * Use to attach a stable id to high-volume posters
                 * (cross-thread signal slots, pipeline-strand
                 * worker callables, etc.) so a per-tick activity
                 * report can identify the heaviest contributors
                 * to the @ref Report::callables bucket.
                 *
                 * Thread-safe.  Cost when no monitor is installed
                 * is one extra @c uint64_t field stored in the
                 * queued item; no registry lookup or
                 * accumulator update happens on the dispatch
                 * path.
                 *
                 * @param label Per-call-site tag.  Construct from
                 *              a string literal at the call site
                 *              via @c Label{"name"}, or pre-mint
                 *              once and reuse — both forms hash
                 *              to the same id.
                 * @param func  The callable to execute.
                 */
                void postCallable(Label label, std::function<void()> func);

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

                /**
                 * @brief One snapshot of where this loop spent its wallclock time.
                 *
                 * Produced by @ref peekStats and @ref consumeStats.
                 * All buckets PLUS every entry in
                 * @ref eventsByType and @ref callablesByLabel sum
                 * (within measurement noise) to @ref wallElapsed.
                 *
                 * @par Partition rule
                 * Each nanosecond is attributed to exactly one
                 * bucket.  Specifically:
                 *  - An event dispatch with a registered
                 *    @c Event::type lands in
                 *    @ref eventsByType keyed on that type — never
                 *    in the aggregate @ref events bucket.
                 *  - A callable dispatch with a non-invalid
                 *    @ref Label lands in @ref callablesByLabel
                 *    keyed on the label id — never in the
                 *    aggregate @ref callables bucket.
                 *  - The aggregate @ref events / @ref callables
                 *    buckets carry only the "unlabeled remainder":
                 *    events posted with @c Event::InvalidType, and
                 *    callables posted via the unlabeled
                 *    @c postCallable overload.
                 *
                 * @ref sleep and @ref queueWait are mutually
                 * exclusive — one is always zero depending on
                 * whether the loop is using POSIX wake fds (the
                 * @c poll() path) or the queue-based fallback
                 * (@c Queue::pop).  Inspecting which sibling is
                 * non-zero identifies the wait strategy.
                 *
                 * @par Practical implication
                 * In a fully-labeled workload @ref callablesCount
                 * goes to zero — the aggregate becomes a "TODO:
                 * label me" indicator for any new @c postCallable
                 * site that omits a label.  Same for
                 * @ref eventsCount.
                 */
                struct Report {
                        /**
                         * @brief Per-event-type elapsed time and dispatch count.
                         *
                         * Carries both the total time spent in the
                         * @c event() handler for this type and the
                         * number of dispatches, so callers can
                         * compute average per-event latency
                         * (@c elapsed / @c count) for diagnostic
                         * output.  The default formatter sorts
                         * descending on @c elapsed when emitting.
                         */
                        struct EventStat {
                                Duration elapsed;
                                int64_t  count = 0;
                        };

                        String   loopName;          ///< From @ref setName; empty if never set.
                        Duration wallElapsed;       ///< Wall time covered by this snapshot.
                        Duration sleep;             ///< Time blocked in poll() / wake fd (POSIX wait path).
                        Duration queueWait;         ///< Time blocked in @c Queue::pop (non-POSIX fallback).  0 on POSIX.
                        Duration timers;            ///< Time inside timer callbacks.
                        Duration events;            ///< Time inside @c event() dispatch.
                        Duration callables;         ///< Time inside posted-callable execution.
                        Duration io;                ///< Time inside IO source callbacks.
                        Duration overhead;          ///< @ref wallElapsed minus the sum of the above.

                        int64_t  timersCount    = 0; ///< Number of timer callbacks dispatched this snapshot.
                        int64_t  eventsCount    = 0; ///< Number of events dispatched this snapshot.
                        int64_t  callablesCount = 0; ///< Number of posted callables dispatched.
                        int64_t  ioCount        = 0; ///< Number of IO callback fires.

                        /// Per-Event::type() breakdown of the @ref events bucket.
                        HashMap<int, EventStat> eventsByType;

                        /// Per-label breakdown of the @ref callables bucket.
                        ///
                        /// Keys are @ref Label::id() values minted
                        /// at @c postCallable time.  Reverse
                        /// lookup via @c Label::fromId(id).name().
                        /// The default formatter renders the
                        /// top-N entries with a @c cb: prefix
                        /// mirroring the @c evt: rendering of
                        /// @ref eventsByType.
                        HashMap<uint64_t, EventStat> callablesByLabel;
                };

                /**
                 * @brief Reporter callback signature for @ref installMonitor.
                 *
                 * Invoked from the loop's own thread once per
                 * sampling interval.  When the same function is
                 * installed on every loop in the process (via
                 * @c Application::startEventLoopMonitors), it is
                 * invoked concurrently from every loop's thread —
                 * the callback is responsible for its own
                 * thread-safety in that mode.
                 */
                using ReportFunction = std::function<void(const Report &)>;

                /**
                 * @brief Sets the loop's human-readable identity.
                 *
                 * The name is copied into every @ref Report so
                 * diagnostics can name the loop without resorting
                 * to a hex pointer.  Distinct from
                 * @c Thread::name() — the convention is for
                 * @c Thread::start to push the thread name down
                 * into the loop on its way up, but the two are
                 * settable independently.
                 *
                 * Thread-safe; the name is copied under an
                 * internal lock.  An empty name is tolerated and
                 * causes the default formatter to fall back to a
                 * hex pointer.
                 *
                 * @param name The loop's human-readable name.
                 */
                void setName(const String &name);

                /**
                 * @brief Returns the loop's human-readable identity.
                 * @return The name set by @ref setName, or empty.
                 */
                String name() const;

                /**
                 * @brief Installs a periodic stats monitor on this loop.
                 *
                 * Thread-safe; may be called from any thread.  The
                 * internal sampling timer is armed via @ref startTimer
                 * so the reporter callback runs on this loop's own
                 * thread.  Replaces any monitor already installed —
                 * accumulators are zeroed and the new reporter and
                 * cadence take effect immediately.
                 *
                 * @warning Must NOT be called synchronously from a
                 *          callable, event, timer, or IO handler
                 *          running on @e this loop.  The internal
                 *          stats mutex is non-recursive and is already
                 *          held by the dispatch bracket; the call
                 *          would deadlock.  From inside such a
                 *          handler, defer the install via
                 *          @c postCallable.  External threads (and
                 *          handlers running on a different loop) may
                 *          call freely.
                 *
                 * @param interval Sample period.  Sub-second values
                 *                 are honoured; very short intervals
                 *                 (< 100 ms) spend most of their
                 *                 wallclock budget on the monitor
                 *                 itself.
                 * @param fn       Reporter callback.  Pass an empty
                 *                 function to use the default
                 *                 one-line @c promekiInfo formatter.
                 */
                void installMonitor(const Duration &interval, ReportFunction fn = {});

                /**
                 * @brief Removes any installed monitor.
                 *
                 * Thread-safe.  Pending stats are discarded.
                 * No-op if no monitor is installed.
                 *
                 * @note A late timer fire may still invoke the
                 *       reporter once on the loop's own thread
                 *       after this call returns — the timer cancel
                 *       races the already-queued fire.  The
                 *       snapshot it sees will be all-zero because
                 *       the monitor gate is cleared first, so the
                 *       reporter receives a clean no-op invocation.
                 *
                 * @warning Same re-entrancy restriction as
                 *          @ref installMonitor: not safe to call
                 *          synchronously from a handler running on
                 *          this loop; defer via @c postCallable.
                 */
                void removeMonitor();

                /**
                 * @brief Returns @c true while a monitor is installed on this loop.
                 *
                 * Useful for tests; the dispatch hot path uses an
                 * internal atomic for the same check.
                 *
                 * @return @c true while a monitor is installed.
                 */
                bool hasMonitor() const;

                /**
                 * @brief Returns a stats snapshot WITHOUT touching the accumulators.
                 *
                 * Safe to call repeatedly from any thread.  Returns
                 * an all-zero @ref Report (with a zero
                 * @c wallElapsed) when no monitor is installed.
                 * Useful for ad-hoc inspection and for tests that
                 * want to observe in-flight counters without
                 * resetting them.
                 *
                 * @return A snapshot of the current accumulators.
                 */
                Report peekStats() const;

                /**
                 * @brief Returns a stats snapshot and atomically zeros the accumulators.
                 *
                 * Called by the monitor's sampling timer; exposed
                 * for tests.  Returns an all-zero @ref Report when
                 * no monitor is installed.
                 *
                 * @return A snapshot of the accumulators since the
                 *         previous @c consumeStats call.
                 */
                Report consumeStats();

        private:
                struct CallableItem {
                                std::function<void()> func;
                                /// Per-call-site label id for activity-monitor
                                /// bucketing.  @c Label::InvalidID for
                                /// unlabeled posts.  Stored unconditionally
                                /// (i.e. even when no monitor is installed)
                                /// to keep the dispatch path branch-free; the
                                /// cost is one @c uint64_t per queued
                                /// callable.
                                uint64_t              labelId = Label::InvalidID;
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

                // ----------------------------------------------------
                // Stats / monitor support.  The hot-path bracket reads
                // _monitorActive once at construction; when false it
                // skips the timestamp grab, the lock, and the
                // accumulator update.  When true it stamps once on
                // construction and once on destruction, takes the
                // mutex, adds the elapsed nanoseconds + 1 to the
                // attributed duration / count buckets, and updates
                // _eventsByType when an event-type is supplied.
                // ----------------------------------------------------

                struct EventStatNs {
                                int64_t elapsed = 0;
                                int64_t count   = 0;
                };

                Atomic<bool>                _monitorActive;
                mutable Mutex               _statsMutex;
                TimeStamp                   _statsLastSnapshot;

                int64_t                     _sleepNs        = 0;
                int64_t                     _queueWaitNs    = 0;
                int64_t                     _timersNs       = 0;
                int64_t                     _eventsNs       = 0;
                int64_t                     _callablesNs    = 0;
                int64_t                     _ioNs           = 0;

                int64_t                     _timersCount    = 0;
                int64_t                     _eventsCount    = 0;
                int64_t                     _callablesCount = 0;
                int64_t                     _ioCount        = 0;

                HashMap<int, EventStatNs>   _eventsByType;

                /// Per-label callable accumulators.  Same shape
                /// as @ref _eventsByType, keyed on @ref Label
                /// ids; consumeStats clears it.
                HashMap<uint64_t, EventStatNs> _callablesByLabel;

                int                         _monitorTimerId = 0;
                ReportFunction              _monitorFn;
                String                      _name;

                // RAII helper that brackets a single dispatch site.
                // Reads _monitorActive ONCE in the constructor and
                // caches the boolean; both the constructor's
                // TimeStamp::now() grab and the destructor's
                // accumulator update key off that cached value so
                // an enable-mid-bracket cannot leave the destructor
                // trying to attribute time without a start
                // timestamp.  When the cached gate is false, no
                // timestamp is stored and the destructor early-outs
                // without locking.
                class StatsBracket {
                                public:
                                        StatsBracket(EventLoop *loop, int64_t *durBucket,
                                                     int64_t *countBucket);
                                        ~StatsBracket();
                                        void attributeEventType(int type) { _eventType = type; }
                                        void attributeCallableLabel(uint64_t labelId) {
                                                _callableLabel = labelId;
                                        }

                                private:
                                        EventLoop  *_loop;
                                        int64_t    *_durBucket;
                                        int64_t    *_countBucket;
                                        TimeStamp   _start;
                                        int         _eventType = -1;
                                        uint64_t    _callableLabel = Label::InvalidID;
                                        bool        _active = false;
                };

                friend class StatsBracket;

                // Default formatter used when _monitorFn is empty.
                // Defined in eventloop.cpp and exposed via
                // installMonitor's "empty fn = default" semantics.
                static void defaultMonitorReporter(const Report &r);
};

PROMEKI_NAMESPACE_END
