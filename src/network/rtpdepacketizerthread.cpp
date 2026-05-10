/**
 * @file      rtpdepacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpdepacketizerthread.h>

PROMEKI_NAMESPACE_BEGIN

RtpDepacketizerThread::RtpDepacketizerThread(const String &name, uint32_t clockRateHz, size_t depth)
    : _clockRateHz(clockRateHz) {
        Thread::setName(name);
        _stopRequested.setValue(false);
        _inputQueue.setMaxSize(depth);
}

RtpDepacketizerThread::~RtpDepacketizerThread() {
        requestStop();
        // The worker is still inside the user-overridden run() loop
        // until it observes _stopRequested or the input queue's
        // cancel state — wait for it to fully exit before letting
        // ~Thread slice the vtable.  Skipped if we are ourselves on
        // the worker (joining ourselves would deadlock).
        if (!isCurrentThread()) wait();
}

void RtpDepacketizerThread::requestStop() {
        _stopRequested.setValue(true);
        _inputQueue.cancelWaiters();
}

void RtpDepacketizerThread::ensureAnchor(uint32_t rtpTs, const TimeStamp &arrivalSteady) {
        if (_anchor.valid) return;
        _anchor.arrivalT0 = arrivalSteady;
        _anchor.rtpTs0 = rtpTs;
        _anchor.clockRate = _clockRateHz;
        _anchor.valid = true;
}

void RtpDepacketizerThread::resetAnchor() {
        _anchor.reset();
}

TimeStamp RtpDepacketizerThread::captureTimeForRtpTs(uint32_t rtpTs) const {
        return _anchor.captureTimeFor(rtpTs);
}

void RtpDepacketizerThread::run() {
        onStart();
        while (!_stopRequested.value()) {
                auto popped = _inputQueue.pop(PopTimeoutMs);
                if (popped.second() == Error::Cancelled) break;
                if (popped.second() == Error::Timeout) continue;
                if (popped.second() != Error::Ok) continue;
                handlePacket(popped.first());
        }
        onStop();
}

PROMEKI_NAMESPACE_END
