/**
 * @file      rtppacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtppacketizerthread.h>

PROMEKI_NAMESPACE_BEGIN

RtpPacketizerThread::RtpPacketizerThread(const String &name, size_t depth) {
        _stopRequested.setValue(false);
        if (depth > 0) _payloadQueue.setMaxSize(depth);
        Thread::setName(name);
}

RtpPacketizerThread::~RtpPacketizerThread() {
        requestStop();
        // Wait for the worker to exit run() before the vtable
        // slice from ~RtpPacketizerThread → ~Thread completes.
        // Subclass hooks (onStop, packetize) are still callable
        // until the worker observes the cancel and exits the pop
        // loop.  Joining ourselves would deadlock — skip the wait
        // when we are the worker (pathological).
        if (!isCurrentThread()) wait();
}

Error RtpPacketizerThread::pushWork(const RtpFrameWork &work, unsigned int timeoutMs) {
        return _payloadQueue.pushBlocking(work, timeoutMs);
}

void RtpPacketizerThread::requestStop() {
        _stopRequested.setValue(true);
        // Wakes any pop blocked on the empty queue and any push
        // blocked on the full queue with Error::Cancelled.  The
        // pop loop in run() observes this and breaks out without
        // a sentinel value — keeps the queue strongly typed
        // (Queue<Frame> stays Queue<Frame>; no magic value needed).
        _payloadQueue.cancelWaiters();
}

void RtpPacketizerThread::run() {
        onStart();
        while (!_stopRequested.value()) {
                auto r = _payloadQueue.pop();
                if (r.second().isError()) {
                        // Cancelled or fatal queue error — exit
                        // cleanly without invoking the subclass.
                        // Spurious queue errors during normal
                        // operation are not expected; if one shows
                        // up, observing the stop flag on the next
                        // iteration will break us out anyway.
                        break;
                }
                packetize(r.first());
        }
        onStop();
}

PROMEKI_NAMESPACE_END
