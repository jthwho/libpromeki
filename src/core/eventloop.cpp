/**
 * @file      eventloop.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/eventloop.h>
#include <promeki/objectbase.h>
#include <promeki/timerevent.h>
#include <promeki/logger.h>
#include <promeki/platform.h>
#include <promeki/selfpipe.h>

#include <cerrno>
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
        return;
}

EventLoop::~EventLoop() {
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
                std::get<CallableItem>(item).func();
        } else if (std::holds_alternative<EventItem>(item)) {
                auto &ei = std::get<EventItem>(item);
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

        // Process posted items unless excluded
        if (!(flags & ExcludePosted)) {
                for (;;) {
                        auto [item, err] = _queue.tryPop();
                        if (err.isError()) break;
                        if (dispatchItem(item)) return;
                }
        }

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

void EventLoop::postCallable(std::function<void()> func) {
        _queue.push(Item{CallableItem{std::move(func)}});
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

int EventLoop::startTimer(unsigned int intervalMs, std::function<void()> func, bool singleShot) {
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
                        std::function<void()> func;
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
        // event loops.
        for (auto it = toFire.begin(); it != toFire.end(); ++it) {
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
                // Wake fd setup failed; fall back to the condvar path
                // so we at least block on the queue.
                auto [val, err] = _queue.pop(waitMs);
                if (err.isOk()) dispatchItem(val);
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

        // Phase 2: poll outside the lock.
        int pollTimeout = (waitMs == 0) ? -1 : static_cast<int>(waitMs);
        int rc = ::poll(pfds.data(), pfds.size(), pollTimeout);
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
        // semantics.
        for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
                if (it->cb) it->cb(it->fd, it->readyEvents);
        }
#else
        // Non-POSIX fallback: block on the condvar queue as before.
        auto [val, err] = _queue.pop(waitMs);
        if (err.isOk()) dispatchItem(val);
#endif
}

PROMEKI_NAMESPACE_END
