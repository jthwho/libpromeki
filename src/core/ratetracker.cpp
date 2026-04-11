/**
 * @file      ratetracker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ratetracker.h>

PROMEKI_NAMESPACE_BEGIN

static constexpr int64_t kNsPerSecond = 1'000'000'000LL;

RateTracker::RateTracker(int64_t windowMs)
        : _windowMs(windowMs < 1 ? 1 : windowMs) {
        // Seed the window start so the first record() call has a sane
        // reference point for the elapsed-time check.  Using now() here
        // (rather than zero) means the very first query reports zero
        // instead of an absurdly large rate derived from epoch-to-now.
        _windowStartNs = TimeStamp::now().nanoseconds();
}

void RateTracker::record(int64_t bytes) {
        // Both counters are updated with a single fetch_add each.  The
        // two increments are not jointly atomic, but the query path
        // tolerates a transient mismatch (bytes updated, frames not
        // yet) because it converts each independently into a rate.
        if(bytes > 0) _bytes.fetchAndAdd(bytes);
        _frames.fetchAndAdd(1);
}

void RateTracker::rotateIfStale(int64_t nowNs) const {
        // Caller holds _mutex.
        int64_t elapsedNs = nowNs - _windowStartNs;
        int64_t windowNs  = _windowMs * 1'000'000LL;
        if(elapsedNs < windowNs) return;

        // The window has aged past its nominal length.  Take a
        // snapshot, roll it into "last window", and start a fresh
        // window counting forward from now.  Because the atomics are
        // updated concurrently with this read, the snapshot may miss
        // an in-flight increment; that increment simply lands in the
        // next window and is accounted for on the next rotation.
        int64_t b = _bytes.value();
        int64_t f = _frames.value();

        _bytes.setValue(0);
        _frames.setValue(0);
        _windowStartNs = nowNs;

        _lastWindowBytes = b;
        _lastWindowFrames = f;
        _lastWindowElapsedNs = elapsedNs;
        _haveLastWindow = true;
}

double RateTracker::bytesPerSecond() const {
        Mutex::Locker lock(_mutex);
        int64_t nowNs = TimeStamp::now().nanoseconds();
        rotateIfStale(nowNs);

        int64_t elapsedNs = nowNs - _windowStartNs;
        int64_t windowNs  = _windowMs * 1'000'000LL;
        int64_t halfNs    = windowNs / 2;

        // Before half the window has filled we prefer the finished
        // previous window (if any) so that a freshly-rotated counter
        // does not under-report.  After that the current window is
        // long enough to be representative on its own.
        if(elapsedNs < halfNs && _haveLastWindow) {
                if(_lastWindowElapsedNs <= 0) return 0.0;
                return static_cast<double>(_lastWindowBytes) * kNsPerSecond
                        / static_cast<double>(_lastWindowElapsedNs);
        }

        if(elapsedNs <= 0) return 0.0;
        int64_t b = _bytes.value();
        return static_cast<double>(b) * kNsPerSecond
                / static_cast<double>(elapsedNs);
}

double RateTracker::framesPerSecond() const {
        Mutex::Locker lock(_mutex);
        int64_t nowNs = TimeStamp::now().nanoseconds();
        rotateIfStale(nowNs);

        int64_t elapsedNs = nowNs - _windowStartNs;
        int64_t windowNs  = _windowMs * 1'000'000LL;
        int64_t halfNs    = windowNs / 2;

        if(elapsedNs < halfNs && _haveLastWindow) {
                if(_lastWindowElapsedNs <= 0) return 0.0;
                return static_cast<double>(_lastWindowFrames) * kNsPerSecond
                        / static_cast<double>(_lastWindowElapsedNs);
        }

        if(elapsedNs <= 0) return 0.0;
        int64_t f = _frames.value();
        return static_cast<double>(f) * kNsPerSecond
                / static_cast<double>(elapsedNs);
}

void RateTracker::reset() {
        Mutex::Locker lock(_mutex);
        _bytes.setValue(0);
        _frames.setValue(0);
        _windowStartNs = TimeStamp::now().nanoseconds();
        _lastWindowBytes = 0;
        _lastWindowFrames = 0;
        _lastWindowElapsedNs = 0;
        _haveLastWindow = false;
}

PROMEKI_NAMESPACE_END
