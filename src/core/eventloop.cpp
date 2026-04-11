/**
 * @file      eventloop.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/eventloop.h>
#include <promeki/objectbase.h>
#include <promeki/timerevent.h>

PROMEKI_NAMESPACE_BEGIN

thread_local EventLoop *EventLoop::_current = nullptr;

EventLoop *EventLoop::current() {
        return _current;
}

EventLoop::EventLoop() {
        _current = this;
        return;
}

EventLoop::~EventLoop() {
        // Drain any remaining items to free owned Event pointers.
        Item item;
        while(_queue.popOrFail(item)) {
                if(std::holds_alternative<EventItem>(item)) {
                        delete std::get<EventItem>(item).event;
                }
        }
        if(_current == this) _current = nullptr;
        return;
}

int EventLoop::exec() {
        _running.setValue(true);
        _exitCode.setValue(0);
        while(_running.value()) {
                processEvents(WaitForMore);
        }
        return _exitCode.value();
}

// Returns true if a QuitItem was processed.
bool EventLoop::dispatchItem(Item &item) {
        if(std::holds_alternative<CallableItem>(item)) {
                std::get<CallableItem>(item).func();
        } else if(std::holds_alternative<EventItem>(item)) {
                auto &ei = std::get<EventItem>(item);
                ei.receiver->event(ei.event);
                delete ei.event;
        } else if(std::holds_alternative<QuitItem>(item)) {
                _exitCode.setValue(std::get<QuitItem>(item).code);
                _running.setValue(false);
                return true;
        }
        return false;
}

void EventLoop::processEvents(uint32_t flags, unsigned int timeoutMs) {
        // Process timers unless excluded
        if(!(flags & ExcludeTimers)) {
                processTimers();
        }

        // Process posted items unless excluded
        if(!(flags & ExcludePosted)) {
                Item item;
                while(_queue.popOrFail(item)) {
                        if(dispatchItem(item)) return;
                }
        }

        // If WaitForMore, block until something arrives or next timer fires
        if(flags & WaitForMore) {
                unsigned int waitMs = timeoutMs;

                // If we have timers, cap wait to next timer fire
                if(!(flags & ExcludeTimers)) {
                        unsigned int timerMs = nextTimerTimeout();
                        if(timerMs > 0) {
                                if(waitMs == 0 || timerMs < waitMs) {
                                        waitMs = timerMs;
                                }
                        }
                }

                auto [val, err] = _queue.pop(waitMs);
                if(err.isOk()) {
                        dispatchItem(val);
                }
        }
        return;
}

void EventLoop::quit(int returnCode) {
        _queue.push(Item{QuitItem{returnCode}});
        notifyWake();
        return;
}

void EventLoop::postCallable(std::function<void()> func) {
        _queue.push(Item{CallableItem{std::move(func)}});
        notifyWake();
        return;
}

void EventLoop::postEvent(ObjectBase *receiver, Event *event) {
        _queue.push(Item{EventItem{receiver, event}});
        notifyWake();
        return;
}

void EventLoop::setWakeCallback(std::function<void()> cb) {
        Mutex::Locker lock(_wakeMutex);
        _wakeCallback = std::move(cb);
        return;
}

void EventLoop::notifyWake() {
        // Grab a local copy so we can invoke outside the lock.  The
        // wake callback pushes an SDL event (or similar) which is
        // cheap but shouldn't be held under the wake mutex in case
        // some future callback grows teeth.
        std::function<void()> cb;
        {
                Mutex::Locker lock(_wakeMutex);
                cb = _wakeCallback;
        }
        if(cb) cb();
        return;
}

int EventLoop::startTimer(ObjectBase *receiver, unsigned int intervalMs,
                           bool singleShot) {
        int id = _nextTimerId.fetchAndAdd(1);
        TimerInfo info;
        info.id = id;
        info.receiver = receiver;
        info.func = nullptr;
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() +
                std::chrono::milliseconds(intervalMs);
        {
                Mutex::Locker lock(_timersMutex);
                _timers += info;
        }
        wakeIfCrossThread();
        return id;
}

int EventLoop::startTimer(unsigned int intervalMs,
                           std::function<void()> func,
                           bool singleShot) {
        int id = _nextTimerId.fetchAndAdd(1);
        TimerInfo info;
        info.id = id;
        info.receiver = nullptr;
        info.func = std::move(func);
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() +
                std::chrono::milliseconds(intervalMs);
        {
                Mutex::Locker lock(_timersMutex);
                _timers += info;
        }
        wakeIfCrossThread();
        return id;
}

void EventLoop::stopTimer(int timerId) {
        {
                Mutex::Locker lock(_timersMutex);
                _timers.removeIf([timerId](const TimerInfo &t) {
                        return t.id == timerId;
                });
        }
        wakeIfCrossThread();
        return;
}

void EventLoop::wakeIfCrossThread() {
        // If the caller is not the owning thread, the owning thread is
        // likely sleeping in Queue::pop(waitMs) and needs to be woken
        // so it re-evaluates nextTimerTimeout().  Pushing a no-op
        // CallableItem is the cheapest way to do that: the dispatcher
        // invokes the empty lambda (a no-op) and the loop proceeds to
        // recompute timer state.  Same-thread callers skip the wake
        // because the loop is not asleep — it is executing the code
        // that called us, and the next processEvents() iteration will
        // pick up the change naturally.
        if(_current == this) return;
        postCallable([](){});
}

void EventLoop::processTimers() {
        // Two-phase: take the lock, scan for ready-to-fire entries,
        // snapshot everything needed to invoke them, rearm or drop
        // each fired entry, release the lock.  Callback invocation
        // runs outside the lock so that a timer body can safely call
        // startTimer() or stopTimer() on the same event loop without
        // deadlocking against _timersMutex.
        struct ReadyTimer {
                int                     id;
                ObjectBase              *receiver;
                std::function<void()>   func;
        };
        List<ReadyTimer> toFire;

        {
                Mutex::Locker lock(_timersMutex);
                if(_timers.isEmpty()) return;
                TimeStamp now = TimeStamp::now();
                List<int> expired;
                for(size_t i = 0; i < _timers.size(); i++) {
                        auto &timer = _timers[i];
                        if(now.value() >= timer.nextFire.value()) {
                                ReadyTimer rt;
                                rt.id = timer.id;
                                rt.receiver = timer.receiver;
                                rt.func = timer.func;  // copy callable
                                toFire += rt;
                                if(timer.singleShot) {
                                        expired += timer.id;
                                } else {
                                        timer.nextFire = now +
                                                std::chrono::milliseconds(timer.intervalMs);
                                }
                        }
                }
                for(int id : expired) {
                        _timers.removeIf([id](const TimerInfo &t) {
                                return t.id == id;
                        });
                }
        }

        // Fire outside the lock.  A callback that calls stopTimer()
        // on a later timer in the same batch will still see that
        // later timer fire once — we copied its callable above — but
        // will succeed in removing the entry from _timers so it does
        // not fire again.  Matches the behavior of most mainstream
        // event loops.
        for(auto it = toFire.begin(); it != toFire.end(); ++it) {
                if(it->receiver != nullptr) {
                        TimerEvent te(it->id);
                        it->receiver->event(&te);
                } else if(it->func) {
                        it->func();
                }
        }
        return;
}

unsigned int EventLoop::nextTimerTimeout() const {
        Mutex::Locker lock(_timersMutex);
        if(_timers.isEmpty()) return 0;
        TimeStamp now = TimeStamp::now();
        unsigned int minMs = UINT_MAX;
        for(const auto &timer : _timers) {
                if(now.value() >= timer.nextFire.value()) {
                        return 1; // Fire immediately on next iteration
                }
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
                        timer.nextFire.value() - now.value());
                unsigned int ms = static_cast<unsigned int>(diff.count());
                if(ms == 0) ms = 1; // Sub-millisecond remaining, wake soon
                if(ms < minMs) minMs = ms;
        }
        return minMs == UINT_MAX ? 0 : minMs;
}

PROMEKI_NAMESPACE_END
