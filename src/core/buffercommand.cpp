/**
 * @file      buffercommand.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/buffercommand.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <utility>

PROMEKI_NAMESPACE_BEGIN

const char *BufferCommand::kindName(Kind k) {
        switch (k) {
                case Map:   return "Map";
                case Unmap: return "Unmap";
                case Copy:  return "Copy";
        }
        return "Unknown";
}

void BufferCommand::markCompleted() {
        // Single-writer latch: only the first call mutates state.
        // The CV broadcast and callback dispatch happen exactly once
        // even if the cancellation path fires twice or a misbehaving
        // backend re-completes the command.
        bool expected = false;
        if (!_completed.compareAndSwap(expected, true)) return;

        {
                Mutex::Locker lock(_completionMutex);
                _completionCv.wakeAll();
        }

        // Pop the callback under its own mutex so a concurrent
        // setCompletionCallback racing against this completion either
        // sees an unresolved command (and stores its callback — we
        // fire it just below) or sees the latched flag (and fires the
        // callback inline itself).  Either path runs the callback
        // exactly once.
        std::function<void(Error)> cb;
        EventLoop                 *loop = nullptr;
        {
                Mutex::Locker lock(_callbackMutex);
                if (_callback) {
                        cb = std::move(_callback);
                        loop = _callbackLoop;
                        _callback = std::function<void(Error)>();
                        _callbackLoop = nullptr;
                }
        }
        if (!cb) return;
        const Error err = result;
        if (loop != nullptr) {
                loop->postCallable([cb = std::move(cb), err]() mutable { cb(err); });
        } else {
                cb(err);
        }
}

Error BufferCommand::waitForCompletion(unsigned int timeoutMs) const {
        if (_completed.value()) return Error::Ok;
        Mutex::Locker lock(_completionMutex);
        if (timeoutMs == 0) {
                while (!_completed.value()) {
                        _completionCv.wait(_completionMutex);
                }
                return Error::Ok;
        }
        ElapsedTimer t;
        t.start();
        while (!_completed.value()) {
                const int64_t elapsed = t.elapsed();
                if (elapsed >= static_cast<int64_t>(timeoutMs)) return Error::Timeout;
                const unsigned int remaining = timeoutMs - static_cast<unsigned int>(elapsed);
                Error              err = _completionCv.wait(_completionMutex, remaining);
                if (err == Error::Timeout && !_completed.value()) return Error::Timeout;
        }
        return Error::Ok;
}

void BufferCommand::setCompletionCallback(std::function<void(Error)> cb, EventLoop *loop) {
        bool fireNow = false;
        {
                Mutex::Locker lock(_callbackMutex);
                if (_completed.value()) {
                        fireNow = true;
                } else {
                        _callback = std::move(cb);
                        _callbackLoop = loop;
                }
        }
        if (!fireNow) return;
        const Error err = result;
        if (loop != nullptr) {
                loop->postCallable([cb = std::move(cb), err]() mutable { cb(err); });
        } else {
                cb(err);
        }
}

PROMEKI_NAMESPACE_END
