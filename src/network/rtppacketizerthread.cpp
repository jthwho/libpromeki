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
                        // the steady-state loop and fall into the
                        // drain phase below.
                        break;
                }
                packetize(r.first());
        }
        // Drain phase: when @ref requestStop fires the cancel
        // wakes any blocked @c pop with @c Error::Cancelled, but
        // items already enqueued by the strand on the way to
        // close stay in @c _payloadQueue until somebody reads
        // them.  Without this drain, a clean
        // executeCmd(Close) cascade would silently lose every
        // frame the strand pushed but the packetizer hadn't yet
        // seen — empirically ~3-4 video frames at 60 fps
        // because the strand is faster than the packetizer for
        // those few frames.  @c tryPop returns immediately when
        // the queue is empty (and the cancel latch is set, so it
        // won't re-block on an empty-and-cancelled queue), so
        // the drain is bounded by the queue depth and the
        // packetizer's per-frame cost.
        while (true) {
                auto r = _payloadQueue.tryPop();
                if (r.second().isError()) break;
                packetize(r.first());
        }
        onStop();
}

PROMEKI_NAMESPACE_END
