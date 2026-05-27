/**
 * @file      rtptxthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtptxthread.h>

PROMEKI_NAMESPACE_BEGIN

RtpTxThread::RtpTxThread(const String &name) {
        _stopRequested.setValue(false);
        Thread::setName(name);
}

RtpTxThread::~RtpTxThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpTxThread::requestStop() {
        // Latch the flag *before* invoking the subclass shutdown
        // hook so any check the hook makes against
        // isStopRequested() reads true.
        const bool wasAlreadyStopped = _stopRequested.exchange(true);
        if (wasAlreadyStopped) return;
        onShutdown();
}

PROMEKI_NAMESPACE_END
