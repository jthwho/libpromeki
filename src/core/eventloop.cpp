/**
 * @file      eventloop.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/function.h>
#include <promeki/eventloop.h>
#include <promeki/application.h>
#include <promeki/objectbase.h>
#include <promeki/timerevent.h>
#include <promeki/logger.h>
#include <promeki/platform.h>
#include <promeki/selfpipe.h>
#include <promeki/list.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <unistd.h>
#include <poll.h>
#if defined(PROMEKI_PLATFORM_LINUX)
#include <sys/eventfd.h>
#endif
#endif

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// EventLoopWakeFd
// ============================================================================
//
// Internal helper that owns the platform wake fd used to interrupt
// poll() when new work is posted to the EventLoop.  On Linux this is
// a single eventfd opened nonblocking + CLOEXEC; on other POSIX
// systems it delegates to SelfPipe.  On non-POSIX platforms the
// helper degrades to a no-op — waitOnSources falls back to the
// Queue condvar path.
//
// The read-side fd is exposed via pollFd() for inclusion in poll()
// sets.  wake() is thread-safe and signal-safe (write() on a pipe
// is; eventfd write is likewise async-signal-safe in practice on
// Linux).  drain() reads until EAGAIN and is invoked on the loop
// thread after poll() returns with the fd readable.
class EventLoopWakeFd {
        public:
                EventLoopWakeFd();
                ~EventLoopWakeFd();

                EventLoopWakeFd(const EventLoopWakeFd &) = delete;
                EventLoopWakeFd &operator=(const EventLoopWakeFd &) = delete;

                /// Returns the fd to monitor for readability (< 0 if not supported).
                int pollFd() const;

                /// Wakes the loop by writing one byte / value to the wake fd.
                void wake();

                /// Drains all pending wakes from the fd.  Called by the loop thread.
                void drain();

        private:
#if defined(PROMEKI_PLATFORM_LINUX)
                int _eventFd = -1;
#elif defined(PROMEKI_PLATFORM_POSIX)
                SelfPipe _pipe;
#endif
};

#if defined(PROMEKI_PLATFORM_LINUX)

EventLoopWakeFd::EventLoopWakeFd() {
        int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (efd < 0) {
                promekiWarn("EventLoop: eventfd() failed (errno %d: %s)", errno, std::strerror(errno));
                return;
        }
        _eventFd = efd;
}

EventLoopWakeFd::~EventLoopWakeFd() {
        if (_eventFd >= 0) ::close(_eventFd);
        _eventFd = -1;
}

int EventLoopWakeFd::pollFd() const {
        return _eventFd;
}

void EventLoopWakeFd::wake() {
        if (_eventFd < 0) return;
        uint64_t one = 1;
        // Ignore EAGAIN: if the counter is already saturated a wake
        // is already pending.  Ignore EINTR: the wake intent is
        // unconditional — another caller or the next post will
        // write again.
        ssize_t n = ::write(_eventFd, &one, sizeof(one));
        (void)n;
}

void EventLoopWakeFd::drain() {
        if (_eventFd < 0) return;
        uint64_t val;
        while (::read(_eventFd, &val, sizeof(val)) == sizeof(val)) {
                // keep draining while saturated wakes remain
        }
}

#elif defined(PROMEKI_PLATFORM_POSIX)

EventLoopWakeFd::EventLoopWakeFd() = default;
EventLoopWakeFd::~EventLoopWakeFd() = default;
int EventLoopWakeFd::pollFd() const {
        return _pipe.readFd();
}
void EventLoopWakeFd::wake() {
        _pipe.wake();
}
void EventLoopWakeFd::drain() {
        _pipe.drain();
}

#else // !PROMEKI_PLATFORM_POSIX

EventLoopWakeFd::EventLoopWakeFd() = default;
EventLoopWakeFd::~EventLoopWakeFd() = default;
int EventLoopWakeFd::pollFd() const {
        return -1;
}
void EventLoopWakeFd::wake() {}
void EventLoopWakeFd::drain() {}

#endif

// ============================================================================
// EventLoop
// ============================================================================

thread_local EventLoop *EventLoop::_current = nullptr;

EventLoop *EventLoop::current() {
        return _current;
}

EventLoop::EventLoop() {
        _current = this;
        _wake = WakeFdUPtr::create();
        // Self-install if Application::startEventLoopMonitors has
        // armed the auto-install hook.  No-op in the common case
        // where no monitoring is configured.  Doing this in the
        // constructor (rather than in Thread::start) means
        // adopted-thread loops and stack-allocated worker loops
        // pick up monitoring too.
        Application::installEventLoopMonitorIfEnabled(this);
        return;
}

EventLoop::~EventLoop() {
        // Tear down the monitor (if any) before the queue / wake fd
        // go away so a late sampling-timer fire cannot land on torn-
        // down state.  removeMonitor is a no-op when no monitor is
        // installed, so this is cheap when the feature is unused.
        if (_monitorActive.value() || _monitorTimerId != 0) {
                removeMonitor();
        }
        // Drain any remaining items to free owned Event pointers.
        for (;;) {
                auto [item, err] = _queue.tryPop();
                if (err.isError()) break;
                if (std::holds_alternative<EventItem>(item)) {
                        delete std::get<EventItem>(item).event;
                }
        }
        _wake.clear();
        if (_current == this) _current = nullptr;
        return;
}

int EventLoop::exec() {
        _running.setValue(true);
        _exitCode.setValue(0);
        while (_running.value()) {
                processEvents(WaitForMore);
        }
        return _exitCode.value();
}

// Returns true if a QuitItem was processed.
bool EventLoop::dispatchItem(Item &item) {
        if (std::holds_alternative<CallableItem>(item)) {
                auto        &ci = std::get<CallableItem>(item);
                StatsBracket bracket(this, &_callablesNs, &_callablesCount);
                bracket.attributeCallableLabel(ci.labelId);
                ci.func();
        } else if (std::holds_alternative<EventItem>(item)) {
                auto        &ei = std::get<EventItem>(item);
                StatsBracket bracket(this, &_eventsNs, &_eventsCount);
                if (ei.event->type() != Event::InvalidType) {
                        bracket.attributeEventType(static_cast<int>(ei.event->type()));
                }
                ei.receiver->event(ei.event);
                delete ei.event;
        } else if (std::holds_alternative<QuitItem>(item)) {
                _exitCode.setValue(std::get<QuitItem>(item).code);
                _running.setValue(false);
                return true;
        }
        return false;
}

void EventLoop::processEvents(uint32_t flags, unsigned int timeoutMs) {
        // Process timers unless excluded
        if (!(flags & ExcludeTimers)) {
                processTimers();
        }

        bool sawQuit = false;

        // Process posted items unless excluded.  Drain the queue to
        // empty even after a QuitItem is dispatched: teardown patterns
        // (@c ObjectBase::deleteLater followed by a quit) post cleanup
        // callables from inside the destructor that needs to run on
        // this loop, and bailing on QuitItem leaks them — and worse,
        // the queue's destructor walks them later and frees lambda
        // storage that something else might still try to dispatch.
        if (!(flags & ExcludePosted)) {
                for (;;) {
                        auto [item, err] = _queue.tryPop();
                        if (err.isError()) break;
                        if (dispatchItem(item)) sawQuit = true;
                }
        }

        // Don't fall through to WaitForMore once a quit has landed —
        // that would block indefinitely with the loop already marked
        // stopped.
        if (sawQuit) return;

        // If WaitForMore, block until something arrives or next timer fires
        if (flags & WaitForMore) {
                unsigned int waitMs = timeoutMs;

                // If we have timers, cap wait to next timer fire
                if (!(flags & ExcludeTimers)) {
                        unsigned int timerMs = nextTimerTimeout();
                        if (timerMs > 0) {
                                if (waitMs == 0 || timerMs < waitMs) {
                                        waitMs = timerMs;
                                }
                        }
                }

                waitOnSources(waitMs);
        }
        return;
}

void EventLoop::quit(int returnCode) {
        _queue.push(Item{QuitItem{returnCode}});
        wakeSelf();
        return;
}

void EventLoop::postCallable(Function<void()> func) {
        _queue.push(Item{CallableItem{std::move(func), Label::InvalidID}});
        wakeSelf();
        return;
}

void EventLoop::postCallable(Label label, Function<void()> func) {
        _queue.push(Item{CallableItem{std::move(func), label.id()}});
        wakeSelf();
        return;
}

void EventLoop::postEvent(ObjectBase *receiver, Event *event) {
        _queue.push(Item{EventItem{receiver, event}});
        wakeSelf();
        return;
}

void EventLoop::wakeSelf() {
        if (_wake.isValid()) _wake->wake();
        return;
}

int EventLoop::startTimer(ObjectBase *receiver, unsigned int intervalMs, bool singleShot) {
        int       id = _nextTimerId.fetchAndAdd(1);
        TimerInfo info;
        info.id = id;
        info.receiver = receiver;
        info.func = nullptr;
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() + std::chrono::milliseconds(intervalMs);
        {
                Mutex::Locker lock(_timersMutex);
                _timers += info;
        }
        wakeSelf();
        return id;
}

int EventLoop::startTimer(unsigned int intervalMs, Function<void()> func, bool singleShot) {
        int       id = _nextTimerId.fetchAndAdd(1);
        TimerInfo info;
        info.id = id;
        info.receiver = nullptr;
        info.func = std::move(func);
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() + std::chrono::milliseconds(intervalMs);
        {
                Mutex::Locker lock(_timersMutex);
                _timers += info;
        }
        wakeSelf();
        return id;
}

void EventLoop::stopTimer(int timerId) {
        {
                Mutex::Locker lock(_timersMutex);
                _timers.removeIf([timerId](const TimerInfo &t) { return t.id == timerId; });
        }
        wakeSelf();
        return;
}

void EventLoop::processTimers() {
        // Two-phase: take the lock, scan for ready-to-fire entries,
        // snapshot everything needed to invoke them, rearm or drop
        // each fired entry, release the lock.  Callback invocation
        // runs outside the lock so that a timer body can safely call
        // startTimer() or stopTimer() on the same event loop without
        // deadlocking against _timersMutex.
        struct ReadyTimer {
                        int                   id;
                        ObjectBase           *receiver;
                        Function<void()> func;
        };
        List<ReadyTimer> toFire;

        {
                Mutex::Locker lock(_timersMutex);
                if (_timers.isEmpty()) return;
                TimeStamp now = TimeStamp::now();
                List<int> expired;
                for (size_t i = 0; i < _timers.size(); i++) {
                        auto &timer = _timers[i];
                        if (now.value() >= timer.nextFire.value()) {
                                ReadyTimer rt;
                                rt.id = timer.id;
                                rt.receiver = timer.receiver;
                                rt.func = timer.func; // copy callable
                                toFire += rt;
                                if (timer.singleShot) {
                                        expired += timer.id;
                                } else {
                                        timer.nextFire = now + std::chrono::milliseconds(timer.intervalMs);
                                }
                        }
                }
                for (int id : expired) {
                        _timers.removeIf([id](const TimerInfo &t) { return t.id == id; });
                }
        }

        // Fire outside the lock.  A callback that calls stopTimer()
        // on a later timer in the same batch will still see that
        // later timer fire once — we copied its callable above — but
        // will succeed in removing the entry from _timers so it does
        // not fire again.  Matches the behavior of most mainstream
        // event loops.  Each fire is individually bracketed so a
        // long-running timer accumulates exactly its share of the
        // _timersNs bucket; bracketing the outer loop instead would
        // attribute the cumulative time as a single dispatch.
        for (auto it = toFire.begin(); it != toFire.end(); ++it) {
                StatsBracket bracket(this, &_timersNs, &_timersCount);
                if (it->receiver != nullptr) {
                        TimerEvent te(it->id);
                        it->receiver->event(&te);
                } else if (it->func) {
                        it->func();
                }
        }
        return;
}

unsigned int EventLoop::nextTimerTimeout() const {
        Mutex::Locker lock(_timersMutex);
        if (_timers.isEmpty()) return 0;
        TimeStamp    now = TimeStamp::now();
        unsigned int minMs = UINT_MAX;
        for (const auto &timer : _timers) {
                if (now.value() >= timer.nextFire.value()) {
                        return 1; // Fire immediately on next iteration
                }
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(timer.nextFire.value() - now.value());
                unsigned int ms = static_cast<unsigned int>(diff.count());
                if (ms == 0) ms = 1; // Sub-millisecond remaining, wake soon
                if (ms < minMs) minMs = ms;
        }
        return minMs == UINT_MAX ? 0 : minMs;
}

// ============================================================================
// I/O source registration
// ============================================================================

int EventLoop::addIoSource(int fd, uint32_t events, IoCallback cb) {
#if defined(PROMEKI_PLATFORM_POSIX)
        if (fd < 0) {
                promekiWarn("EventLoop::addIoSource: invalid fd %d", fd);
                return -1;
        }
        if (!cb) {
                promekiWarn("EventLoop::addIoSource: empty callback");
                return -1;
        }
        if ((events & (IoRead | IoWrite)) == 0) {
                promekiWarn("EventLoop::addIoSource: no readable/writable event bits set");
                return -1;
        }
        int      handle = _nextIoHandle.fetchAndAdd(1);
        IoSource src;
        src.handle = handle;
        src.fd = fd;
        src.events = events;
        src.cb = std::move(cb);
        src.pendingRemove = false;
        {
                Mutex::Locker lock(_ioMutex);
                _ioSources += src;
        }
        // Wake the loop so the next poll() picks up the new fd.
        wakeSelf();
        return handle;
#else
        (void)fd;
        (void)events;
        (void)cb;
        promekiWarn("EventLoop::addIoSource: not implemented on this platform");
        return -1;
#endif
}

void EventLoop::removeIoSource(int handle) {
#if defined(PROMEKI_PLATFORM_POSIX)
        if (handle < 0) return;
        {
                Mutex::Locker lock(_ioMutex);
                for (size_t i = 0; i < _ioSources.size(); i++) {
                        if (_ioSources[i].handle == handle) {
                                // Mark pendingRemove rather than erasing
                                // in-place.  The loop thread sweeps
                                // pending entries at the top of
                                // waitOnSources before building the
                                // next poll set, which avoids
                                // invalidating a dispatch snapshot that
                                // may be mid-iteration.
                                _ioSources[i].pendingRemove = true;
                                break;
                        }
                }
        }
        // Wake the loop so the pending-remove sweep runs before the
        // next poll; this guarantees a just-removed fd won't fire
        // another callback.
        wakeSelf();
#else
        (void)handle;
#endif
}

void EventLoop::waitOnSources(unsigned int waitMs) {
#if defined(PROMEKI_PLATFORM_POSIX)
        if (_wake.isNull() || _wake->pollFd() < 0) {
                // Wake fd setup failed; fall back to the condvar
                // path so we at least block on the queue.  Bracket
                // the queue wait into _queueWaitNs so the snapshot
                // self-describes which wait strategy this loop is
                // on.
                Item  item;
                Error err;
                {
                        StatsBracket waitBracket(this, &_queueWaitNs, nullptr);
                        auto         result = _queue.pop(waitMs);
                        item = std::move(result.first());
                        err = result.second();
                }
                // Dispatch outside the wait bracket so the dispatch
                // time is attributed to its real bucket
                // (_callablesNs / _eventsNs / ...).
                if (err.isOk()) dispatchItem(item);
                return;
        }

        // Phase 1: sweep pendingRemove entries and build the poll
        // set under the ioMutex.  We also snapshot (handle, cb,
        // events, fd) for each live source so callbacks can be
        // fired outside the lock.
        struct Ready {
                        int        handle;
                        int        fd;
                        uint32_t   readyEvents;
                        IoCallback cb;
        };
        List<pollfd> pfds;
        List<Ready>  snapshot;
        {
                Mutex::Locker lock(_ioMutex);
                _ioSources.removeIf([](const IoSource &s) { return s.pendingRemove; });
                pfds.reserve(_ioSources.size() + 1);
                pollfd wakePfd;
                wakePfd.fd = _wake->pollFd();
                wakePfd.events = POLLIN;
                wakePfd.revents = 0;
                pfds += wakePfd;
                for (size_t i = 0; i < _ioSources.size(); i++) {
                        const IoSource &src = _ioSources[i];
                        pollfd          pfd;
                        pfd.fd = src.fd;
                        pfd.events = 0;
                        if (src.events & IoRead) pfd.events |= POLLIN;
                        if (src.events & IoWrite) pfd.events |= POLLOUT;
                        pfd.revents = 0;
                        pfds += pfd;
                }
        }

        // Phase 2: poll outside the lock.  Bracket the syscall into
        // _sleepNs so the snapshot reflects how much wallclock the
        // loop spent waiting for work versus dispatching it.
        int pollTimeout = (waitMs == 0) ? -1 : static_cast<int>(waitMs);
        int rc;
        {
                StatsBracket sleepBracket(this, &_sleepNs, nullptr);
                rc = ::poll(pfds.data(), pfds.size(), pollTimeout);
        }
        if (rc < 0) {
                if (errno == EINTR) return;
                promekiWarn("EventLoop::waitOnSources: poll() failed (errno %d: %s)", errno, std::strerror(errno));
                return;
        }
        if (rc == 0) return; // timeout

        // Phase 3: drain wake fd, drain queue, snapshot ready
        // callbacks under the lock, fire outside the lock.
        if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
                _wake->drain();
        }

        // Drain posted items now so any quit/callable queued during
        // the poll is processed immediately — matches the semantics
        // of the previous Queue::pop(waitMs) path.
        for (;;) {
                auto [item, err] = _queue.tryPop();
                if (err.isError()) break;
                if (dispatchItem(item)) {
                        // Quit seen; no point dispatching further.
                        return;
                }
        }

        // Collect ready I/O sources (index 1+ of pfds).  Re-lock
        // briefly to snapshot the callbacks; a removeIoSource
        // racing with this sweep has already marked pendingRemove,
        // so we skip those.
        {
                Mutex::Locker lock(_ioMutex);
                // pfds built from _ioSources under the same lock.
                // Positions are stable as long as the list hasn't
                // been mutated since — which is guaranteed because
                // addIoSource only appends and pendingRemove doesn't
                // change positions.  Any new addIoSource calls
                // happened outside the lock and aren't yet in pfds.
                size_t n = _ioSources.size();
                for (size_t i = 0; i < n && (i + 1) < pfds.size(); i++) {
                        const pollfd &pfd = pfds[i + 1];
                        if (pfd.revents == 0) continue;
                        const IoSource &src = _ioSources[i];
                        if (src.pendingRemove) continue;
                        uint32_t ready = 0;
                        if (pfd.revents & POLLIN) ready |= IoRead;
                        if (pfd.revents & POLLOUT) ready |= IoWrite;
                        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                                ready |= IoError;
                        }
                        if (ready == 0) continue;
                        Ready r;
                        r.handle = src.handle;
                        r.fd = src.fd;
                        r.readyEvents = ready;
                        r.cb = src.cb; // copy callable
                        snapshot += r;
                }
        }

        // Fire outside the lock so callbacks can mutate the source
        // list without deadlocking.  A callback that removeIoSource's
        // a later entry in the batch still sees that later entry
        // fire once (we copied its callable), matching timer
        // semantics.  Each callback is individually bracketed so a
        // long-running source's time accumulates one-fire-at-a-time
        // into _ioNs / _ioCount.
        for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
                if (!it->cb) continue;
                StatsBracket bracket(this, &_ioNs, &_ioCount);
                it->cb(it->fd, it->readyEvents);
        }
#else
        // Non-POSIX fallback: block on the condvar queue as before.
        // The condvar wait is attributed to _queueWaitNs so the
        // snapshot self-describes the wait strategy in use.
        Item  item;
        Error err;
        {
                StatsBracket waitBracket(this, &_queueWaitNs, nullptr);
                auto         result = _queue.pop(waitMs);
                item = std::move(result.first());
                err = result.second();
        }
        if (err.isOk()) dispatchItem(item);
#endif
}

// ============================================================================
// Stats / monitor support
// ============================================================================

EventLoop::StatsBracket::StatsBracket(EventLoop *loop, int64_t *durBucket, int64_t *countBucket)
    : _loop(loop), _durBucket(durBucket), _countBucket(countBucket) {
        // Read the gate ONCE so an enable-mid-bracket cannot leave
        // the destructor trying to attribute time without a start
        // timestamp.  When the cached gate is false the constructor
        // and destructor are both early-out (no syscall, no lock).
        if (!_loop->_monitorActive.value()) return;
        _active = true;
        _start = TimeStamp::now();
        return;
}

EventLoop::StatsBracket::~StatsBracket() {
        if (!_active) return;
        int64_t       elapsedNs = (TimeStamp::now() - _start).nanoseconds();
        Mutex::Locker lock(_loop->_statsMutex);
        // Partition rule: each nanosecond lands in exactly one
        // bucket.  If the bracket is tagged with an event type or a
        // callable label the time goes into that per-key map; the
        // aggregate @c _eventsNs / @c _callablesNs slot only
        // accumulates the "unlabeled remainder" (events with
        // InvalidType, callables posted without a Label).  This
        // keeps the per-tick line purely additive — sleep + each
        // bucket + each evt:/cb: entry + overhead sums to
        // wallElapsed without double counting.
        if (_eventType >= 0) {
                EventStatNs &slot = _loop->_eventsByType[_eventType];
                slot.elapsed += elapsedNs;
                slot.count++;
        } else if (_callableLabel != Label::InvalidID) {
                EventStatNs &slot = _loop->_callablesByLabel[_callableLabel];
                slot.elapsed += elapsedNs;
                slot.count++;
        } else {
                if (_durBucket != nullptr) *_durBucket += elapsedNs;
                if (_countBucket != nullptr) (*_countBucket)++;
        }
        return;
}

void EventLoop::setName(const String &name) {
        Mutex::Locker lock(_statsMutex);
        _name = name;
        return;
}

String EventLoop::name() const {
        Mutex::Locker lock(_statsMutex);
        return _name;
}

bool EventLoop::hasMonitor() const {
        return _monitorActive.value();
}

namespace {

// Bucket math shared by peekStats / consumeStats.  Computes
// wallElapsed = now - lastSnapshot, copies bucket / count
// accumulators into Durations, derives overhead = wall - sum, and
// clamps overhead to [0, +inf) so measurement noise that pushes
// the buckets slightly past wall-elapsed cannot produce a negative
// duration.
//
// Under the partition rule (each ns lands in exactly one bucket),
// the sum must include both the aggregate buckets AND every
// per-key map entry — otherwise time attributed to eventsByType
// or callablesByLabel would leak back into overhead.
void finalizeBuckets(EventLoop::Report &out, const TimeStamp &now, const TimeStamp &lastSnapshot) {
        Duration wall = (lastSnapshot.value().time_since_epoch().count() == 0)
                                ? Duration()
                                : (now - lastSnapshot);
        out.wallElapsed = wall;
        int64_t sumNs = out.sleep.nanoseconds() + out.queueWait.nanoseconds() +
                        out.timers.nanoseconds() + out.events.nanoseconds() +
                        out.callables.nanoseconds() + out.io.nanoseconds();
        for (const auto &[t, es] : out.eventsByType) sumNs += es.elapsed.nanoseconds();
        for (const auto &[id, es] : out.callablesByLabel) sumNs += es.elapsed.nanoseconds();
        int64_t overheadNs = wall.nanoseconds() - sumNs;
        if (overheadNs < 0) overheadNs = 0;
        out.overhead = Duration::fromNanoseconds(overheadNs);
        return;
}

} // namespace

EventLoop::Report EventLoop::peekStats() const {
        Report out;
        if (!_monitorActive.value()) return out;
        Mutex::Locker lock(_statsMutex);
        TimeStamp     now = TimeStamp::now();
        out.loopName = _name;
        out.sleep = Duration::fromNanoseconds(_sleepNs);
        out.queueWait = Duration::fromNanoseconds(_queueWaitNs);
        out.timers = Duration::fromNanoseconds(_timersNs);
        out.events = Duration::fromNanoseconds(_eventsNs);
        out.callables = Duration::fromNanoseconds(_callablesNs);
        out.io = Duration::fromNanoseconds(_ioNs);
        out.timersCount = _timersCount;
        out.eventsCount = _eventsCount;
        out.callablesCount = _callablesCount;
        out.ioCount = _ioCount;
        for (const auto &[type, slot] : _eventsByType) {
                Report::EventStat es;
                es.elapsed = Duration::fromNanoseconds(slot.elapsed);
                es.count = slot.count;
                out.eventsByType.insert(type, es);
        }
        for (const auto &[labelId, slot] : _callablesByLabel) {
                Report::EventStat es;
                es.elapsed = Duration::fromNanoseconds(slot.elapsed);
                es.count = slot.count;
                out.callablesByLabel.insert(labelId, es);
        }
        finalizeBuckets(out, now, _statsLastSnapshot);
        return out;
}

EventLoop::Report EventLoop::consumeStats() {
        Report out;
        if (!_monitorActive.value()) return out;
        Mutex::Locker lock(_statsMutex);
        TimeStamp     now = TimeStamp::now();
        out.loopName = _name;
        out.sleep = Duration::fromNanoseconds(_sleepNs);
        out.queueWait = Duration::fromNanoseconds(_queueWaitNs);
        out.timers = Duration::fromNanoseconds(_timersNs);
        out.events = Duration::fromNanoseconds(_eventsNs);
        out.callables = Duration::fromNanoseconds(_callablesNs);
        out.io = Duration::fromNanoseconds(_ioNs);
        out.timersCount = _timersCount;
        out.eventsCount = _eventsCount;
        out.callablesCount = _callablesCount;
        out.ioCount = _ioCount;
        for (const auto &[type, slot] : _eventsByType) {
                Report::EventStat es;
                es.elapsed = Duration::fromNanoseconds(slot.elapsed);
                es.count = slot.count;
                out.eventsByType.insert(type, es);
        }
        for (const auto &[labelId, slot] : _callablesByLabel) {
                Report::EventStat es;
                es.elapsed = Duration::fromNanoseconds(slot.elapsed);
                es.count = slot.count;
                out.callablesByLabel.insert(labelId, es);
        }
        finalizeBuckets(out, now, _statsLastSnapshot);
        // Zero the accumulators in place under the same lock.
        _sleepNs = 0;
        _queueWaitNs = 0;
        _timersNs = 0;
        _eventsNs = 0;
        _callablesNs = 0;
        _ioNs = 0;
        _timersCount = 0;
        _eventsCount = 0;
        _callablesCount = 0;
        _ioCount = 0;
        _eventsByType.clear();
        _callablesByLabel.clear();
        _statsLastSnapshot = now;
        return out;
}

namespace {

// Render a "<prefix>:<name>=PCT%(n=K,~Xus)" tail from a per-key
// breakdown map.  Sorted by elapsed descending; sub-topN entries
// summed into a synthetic "(N others=...%)" suffix.  Sub-0.05%
// entries are skipped to keep the line tight.  @p resolveName
// maps a key to a displayable string; an empty return falls back
// to the literal key prefixed with "id".
template <typename Key, typename ResolveName>
String formatStatsTail(const HashMap<Key, EventLoop::Report::EventStat> &byKey,
                       const Duration &wall, const char *prefix, size_t topN,
                       ResolveName &&resolveName) {
        if (byKey.isEmpty()) return String();
        struct Entry {
                        Key      key;
                        int64_t  elapsedNs;
                        int64_t  count;
        };
        List<Entry> entries;
        entries.reserve(byKey.size());
        for (const auto &[k, es] : byKey) {
                Entry e;
                e.key = k;
                e.elapsedNs = es.elapsed.nanoseconds();
                e.count = es.count;
                entries += e;
        }
        entries.sortInPlace([](const Entry &a, const Entry &b) { return a.elapsedNs > b.elapsedNs; });
        const double wallNs = static_cast<double>(wall.nanoseconds());
        String       out;
        size_t       emit = topN == 0 ? entries.size() : std::min(topN, entries.size());
        for (size_t i = 0; i < emit; i++) {
                const Entry &e = entries[i];
                double pct = wallNs > 0.0 ? 100.0 * static_cast<double>(e.elapsedNs) / wallNs : 0.0;
                if (pct < 0.05) {
                        emit = i;
                        break;
                }
                String name = resolveName(e.key);
                if (name.isEmpty()) name = String("id") + String::number(static_cast<int64_t>(e.key));
                char pctBuf[32];
                std::snprintf(pctBuf, sizeof(pctBuf), "%.1f", pct);
                out += String("  ") + prefix + ":" + name + "=" + pctBuf + "%(n=" +
                       String::number(e.count);
                if (e.count > 0) {
                        int64_t avgUs = (e.elapsedNs / e.count) / 1000;
                        out += String(",~") + String::number(avgUs) + "us";
                }
                out += String(")");
        }
        if (topN > 0 && entries.size() > topN) {
                int64_t otherNs = 0;
                int64_t otherCount = 0;
                for (size_t i = topN; i < entries.size(); i++) {
                        otherNs += entries[i].elapsedNs;
                        otherCount += entries[i].count;
                }
                (void)otherCount;
                double otherPct = wallNs > 0.0 ? 100.0 * static_cast<double>(otherNs) / wallNs : 0.0;
                if (otherPct >= 0.05) {
                        char pctBuf[32];
                        std::snprintf(pctBuf, sizeof(pctBuf), "%.1f", otherPct);
                        out += String("  ") + prefix + ":(" +
                               String::number(entries.size() - topN) + " others=" + pctBuf + "%)";
                }
        }
        return out;
}

// Append "label=PCT%(n=N)" to @p out when @p pct is above the
// emit threshold; "(n=N)" is included only when @p countBucket is
// non-negative.  Sub-0.05% buckets are skipped to keep the line
// tight.
void appendBucket(String &out, const char *label, double pct, int64_t count, bool emitCount) {
        if (pct < 0.05) return;
        char pctBuf[32];
        std::snprintf(pctBuf, sizeof(pctBuf), "%.1f", pct);
        out += String("  ") + label + "=" + pctBuf + "%";
        if (emitCount) out += String("(n=") + String::number(count) + ")";
        return;
}

String formatEventLoopReport(const EventLoop::Report &r) {
        char         secBuf[32];
        const double sec = static_cast<double>(r.wallElapsed.nanoseconds()) / 1.0e9;
        std::snprintf(secBuf, sizeof(secBuf), "%.1f", sec);
        String label = r.loopName.isEmpty() ? String("?") : r.loopName;
        String out = String("evloop/") + label + "/" + secBuf + "s";
        const double wallNs = static_cast<double>(r.wallElapsed.nanoseconds());
        auto         pctOf = [wallNs](const Duration &d) {
                return wallNs > 0.0 ? 100.0 * static_cast<double>(d.nanoseconds()) / wallNs : 0.0;
        };
        // sleep / queueWait are mutually exclusive — emit whichever
        // is non-zero so the line cleanly identifies the wait
        // strategy without doubling up.
        if (r.queueWait.nanoseconds() > 0) {
                appendBucket(out, "queueWait", pctOf(r.queueWait), 0, false);
        } else {
                appendBucket(out, "sleep", pctOf(r.sleep), 0, false);
        }
        appendBucket(out, "events", pctOf(r.events), r.eventsCount, true);
        appendBucket(out, "timers", pctOf(r.timers), r.timersCount, true);
        appendBucket(out, "callables", pctOf(r.callables), r.callablesCount, true);
        appendBucket(out, "io", pctOf(r.io), r.ioCount, true);
        appendBucket(out, "overhead", pctOf(r.overhead), 0, false);
        out += formatStatsTail(r.eventsByType, r.wallElapsed, "evt", 4, [](int type) {
                return Event::typeName(static_cast<Event::Type>(type));
        });
        out += formatStatsTail(r.callablesByLabel, r.wallElapsed, "cb", 4, [](uint64_t labelId) {
                return EventLoop::Label::fromId(labelId).name();
        });
        return out;
}

} // namespace

void EventLoop::defaultMonitorReporter(const Report &r) {
        promekiInfo("%s", formatEventLoopReport(r).cstr());
        return;
}

void EventLoop::installMonitor(const Duration &interval, ReportFunction fn) {
        // Replace any existing monitor: stop the existing sampler
        // timer, zero the accumulators, install the new fn, prime
        // _statsLastSnapshot, then arm a fresh timer.
        int oldTimerId = 0;
        {
                Mutex::Locker lock(_statsMutex);
                oldTimerId = _monitorTimerId;
                _monitorTimerId = 0;
                _monitorFn = fn ? std::move(fn) : ReportFunction(&EventLoop::defaultMonitorReporter);
                _sleepNs = 0;
                _queueWaitNs = 0;
                _timersNs = 0;
                _eventsNs = 0;
                _callablesNs = 0;
                _ioNs = 0;
                _timersCount = 0;
                _eventsCount = 0;
                _callablesCount = 0;
                _ioCount = 0;
                _eventsByType.clear();
                _callablesByLabel.clear();
                _statsLastSnapshot = TimeStamp::now();
        }
        // Tear the old timer down outside the lock; startTimer /
        // stopTimer take their own (different) mutex and a callback
        // arming the new timer must observe _monitorActive=true.
        if (oldTimerId != 0) stopTimer(oldTimerId);
        _monitorActive.setValue(true);
        unsigned int intervalMs = static_cast<unsigned int>(std::max<int64_t>(1, interval.milliseconds()));
        int          newTimerId = startTimer(intervalMs, [this]() {
                Report     r = consumeStats();
                ReportFunction fn;
                {
                        Mutex::Locker lock(_statsMutex);
                        fn = _monitorFn;
                }
                if (fn) fn(r);
        });
        {
                Mutex::Locker lock(_statsMutex);
                _monitorTimerId = newTimerId;
        }
        return;
}

void EventLoop::removeMonitor() {
        // Clear the gate first so any in-flight bracket destructor
        // skips its accumulator update; a late sampling-timer fire
        // will then see consumeStats() return an all-zero Report.
        _monitorActive.setValue(false);
        int captured = 0;
        {
                Mutex::Locker lock(_statsMutex);
                captured = _monitorTimerId;
                _monitorTimerId = 0;
                _monitorFn = nullptr;
        }
        if (captured != 0) stopTimer(captured);
        return;
}

PROMEKI_NAMESPACE_END
