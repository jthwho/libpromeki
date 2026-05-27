/**
 * @file      cadence.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cadence.h>

PROMEKI_NAMESPACE_BEGIN

Cadence::Cadence(const Duration &interval) : _interval(interval) {}

void Cadence::anchor(const TimeStamp &t0) {
        _next = t0;
        _anchored = true;
        _ticks = 0;
}

TimeStamp Cadence::next() {
        // The contract is that the caller anchored before the first
        // next() — but defensive callers may forget.  Returning the
        // current cursor (TimeStamp() if unanchored) and advancing
        // anyway lets perf-critical paths skip the anchored check;
        // the unanchored case still produces a deterministic answer.
        const TimeStamp deadline = _next;
        _next += _interval;
        ++_ticks;
        return deadline;
}

void Cadence::reanchor(const TimeStamp &t) {
        // Skip one tick on resume.  If we returned t directly the
        // first emission would be back-to-back with whatever fired
        // just before reanchor() (potentially zero gap), which would
        // burst-emit at exactly the rate the long-stall recovery is
        // trying to avoid.  Adding one interval guarantees a clean
        // gap between stall and resumption.
        _next = t + _interval;
        _anchored = true;
}

uint64_t Cadence::ticks() const {
        return _ticks;
}

Duration Cadence::interval() const {
        return _interval;
}

PROMEKI_NAMESPACE_END
