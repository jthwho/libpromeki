/**
 * @file      ndiclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndiclock.h>

#include <Processing.NDI.Lib.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Default jitter envelope when no frame rate is known yet.
        // ± 50 ms is conservative — covers everything from broadcast
        // 24p (41.7ms frame period) up through low-latency 60p (16.7ms),
        // with headroom for SDK buffering quirks at startup.
        constexpr int64_t kDefaultHalfJitterNs = 50'000'000;

} // namespace

const ClockDomain &NdiClock::domain() {
        // Lazily register the project-wide "Ndi" domain.  Subsequent
        // calls hit the registry's name-based dedup and return the
        // same ID.  Static storage gives us the once-per-process
        // semantics we want without an explicit init step.
        static const ClockDomain::ID id = ClockDomain::registerDomain(
                String("Ndi"),
                String("NDI per-frame timestamp clock (100ns ticks, sender-anchored)"),
                ClockEpoch::Absolute);
        static const ClockDomain dom(id);
        return dom;
}

NdiClock::NdiClock(const FrameRate &frameRate) : Clock(domain()), _frameRate(frameRate) {}

NdiClock::~NdiClock() {
        // Wake any thread parked in sleepUntilNs so the destructor
        // does not deadlock if a consumer is still waiting on this
        // clock when the owning MediaIO is torn down.
        _shutdown.store(true, std::memory_order_release);
        Mutex::Locker lk(_waitMutex);
        _waitCond.wakeAll();
}

void NdiClock::setLatestTimestamp(int64_t ndiTimestampTicks) {
        // The SDK uses INT64_MAX as "undefined timestamp" — silently
        // ignore so the clock doesn't jump to year 30000 on the rare
        // packet without a timestamp.
        if (ndiTimestampTicks == NDIlib_recv_timestamp_undefined) return;
        const int64_t ns = ndiTimestampTicks * 100;
        _lastTimestampNs.store(ns, std::memory_order_release);
        _hasTimestamp.store(true, std::memory_order_release);
        Mutex::Locker lk(_waitMutex);
        _waitCond.wakeAll();
}

void NdiClock::setFrameRate(const FrameRate &frameRate) {
        Mutex::Locker lk(_waitMutex);
        _frameRate = frameRate;
}

int64_t NdiClock::resolutionNs() const {
        // NDI ticks are 100ns; that's the smallest meaningful step
        // even when the source's frame rate is much coarser.
        return 100;
}

ClockJitter NdiClock::jitter() const {
        Mutex::Locker lk(_waitMutex);
        int64_t       half = kDefaultHalfJitterNs;
        if (_frameRate.isValid()) {
                half = _frameRate.frameDuration().nanoseconds() / 2;
                if (half <= 0) half = kDefaultHalfJitterNs;
        }
        return ClockJitter{Duration::fromNanoseconds(-half), Duration::fromNanoseconds(half)};
}

Result<int64_t> NdiClock::raw() const {
        // Pre-first-frame: return zero rather than an error so
        // pipelines bringing up against this clock don't have to
        // special-case the cold-start path.  resolutionNs() and the
        // jitter envelope already document the precision a consumer
        // can expect.
        return makeResult<int64_t>(_lastTimestampNs.load(std::memory_order_acquire));
}

Error NdiClock::sleepUntilNs(int64_t targetNs) const {
        if (_shutdown.load(std::memory_order_acquire)) return Error::Cancelled;
        // Fast path: target already reached.
        if (_lastTimestampNs.load(std::memory_order_acquire) >= targetNs) return Error::Ok;
        Mutex::Locker lk(_waitMutex);
        for (;;) {
                if (_shutdown.load(std::memory_order_acquire)) return Error::Cancelled;
                if (_lastTimestampNs.load(std::memory_order_acquire) >= targetNs) return Error::Ok;
                // Wake periodically so a forgotten signal doesn't
                // strand the waiter — also lets shutdown checks fire
                // without external help.  100ms is short enough to
                // unwind close paths but long enough to stay quiet
                // during steady-state operation.
                _waitCond.wait(_waitMutex, 100);
        }
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
