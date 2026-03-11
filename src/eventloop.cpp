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
        _running.store(true, std::memory_order_relaxed);
        _exitCode.store(0, std::memory_order_relaxed);
        while(_running.load(std::memory_order_relaxed)) {
                processEvents(WaitForMore);
        }
        return _exitCode.load(std::memory_order_relaxed);
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
                _exitCode.store(std::get<QuitItem>(item).code,
                                std::memory_order_relaxed);
                _running.store(false, std::memory_order_relaxed);
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
        return;
}

void EventLoop::postCallable(std::function<void()> func) {
        _queue.push(Item{CallableItem{std::move(func)}});
        return;
}

void EventLoop::postEvent(ObjectBase *receiver, Event *event) {
        _queue.push(Item{EventItem{receiver, event}});
        return;
}

int EventLoop::startTimer(ObjectBase *receiver, unsigned int intervalMs,
                           bool singleShot) {
        int id = _nextTimerId.fetch_add(1, std::memory_order_relaxed);
        TimerInfo info;
        info.id = id;
        info.receiver = receiver;
        info.func = nullptr;
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() +
                std::chrono::milliseconds(intervalMs);
        _timers += info;
        return id;
}

int EventLoop::startTimer(unsigned int intervalMs,
                           std::function<void()> func,
                           bool singleShot) {
        int id = _nextTimerId.fetch_add(1, std::memory_order_relaxed);
        TimerInfo info;
        info.id = id;
        info.receiver = nullptr;
        info.func = std::move(func);
        info.intervalMs = intervalMs;
        info.singleShot = singleShot;
        info.nextFire = TimeStamp::now() +
                std::chrono::milliseconds(intervalMs);
        _timers += info;
        return id;
}

void EventLoop::stopTimer(int timerId) {
        _timers.removeIf([timerId](const TimerInfo &t) {
                return t.id == timerId;
        });
        return;
}

void EventLoop::processTimers() {
        if(_timers.isEmpty()) return;
        TimeStamp now = TimeStamp::now();
        List<int> expired;
        for(size_t i = 0; i < _timers.size(); i++) {
                auto &timer = _timers[i];
                if(now.value() >= timer.nextFire.value()) {
                        if(timer.receiver != nullptr) {
                                TimerEvent te(timer.id);
                                timer.receiver->event(&te);
                        } else if(timer.func) {
                                timer.func();
                        }
                        if(timer.singleShot) {
                                expired += timer.id;
                        } else {
                                timer.nextFire = now +
                                        std::chrono::milliseconds(timer.intervalMs);
                        }
                }
        }
        for(int id : expired) {
                stopTimer(id);
        }
        return;
}

unsigned int EventLoop::nextTimerTimeout() const {
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
