/**
 * @file      pacinggate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pacinggate.h>

#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

PacingGate::PacingGate(const Clock::Ptr &clock, const Duration &period) : _clock(clock), _period(period) {
        // Mirror the threshold defaults so the no-arg setPeriod path
        // and the explicit-period constructor agree on initial values.
        _skipThreshold = period;
        _reanchorThreshold = period * static_cast<int64_t>(DefaultReanchorMultiple);
}

void PacingGate::setClock(const Clock::Ptr &clock) {
        _clock = clock;
        // A different clock means a different timeline — the previous
        // anchor no longer corresponds to anything meaningful, so the
        // next wait() must re-anchor.  This is also true for an
        // explicit unbind (clock = null) so a future re-bind starts
        // fresh.
        rearm();
}

void PacingGate::setPeriod(const Duration &period) {
        _period = period;
        // Track-with-period default: if the caller never explicitly
        // set thresholds, keep them in sync with the new period.
        // Otherwise leave the explicit choice alone — the caller knows
        // why they picked specific values.
        if (!_customSkipThreshold) {
                _skipThreshold = period;
        }
        if (!_customReanchorThreshold) {
                _reanchorThreshold = period * static_cast<int64_t>(DefaultReanchorMultiple);
        }
}

void PacingGate::setSkipThreshold(const Duration &t) {
        _skipThreshold = t;
        _customSkipThreshold = true;
}

void PacingGate::setReanchorThreshold(const Duration &t) {
        _reanchorThreshold = t;
        _customReanchorThreshold = true;
}

void PacingGate::rearm() {
        _armed = false;
        _accumulated = Duration();
}

PacingResult PacingGate::wait(const Duration &advance) {
        PacingResult result;

        // No clock = pacing disabled.  Every wait is a no-op so the
        // backend can call wait() unconditionally without branching on
        // hasClock() at every site.
        if (!_clock.isValid()) return result;

        // First call after construction / rearm() / setClock() — latch
        // the anchor and return immediately so the first work item
        // ships without a sleep.  This matches the typical sender's
        // expectation that the first frame goes out the moment the
        // pump produces it.
        if (!_armed) {
                Result<MediaTimeStamp> nowR = _clock->now();
                if (nowR.second().isError()) {
                        result.error = nowR.second();
                        return result;
                }
                _anchor = nowR.first();
                _accumulated = Duration();
                _armed = true;
                ++_ticksOnTime;
                return result;
        }

        // Advance the timeline by the caller-supplied period and
        // compute the absolute deadline against the anchor.
        _accumulated = _accumulated + advance;
        TimeStamp      deadlineTs = _anchor.timeStamp() + _accumulated;
        MediaTimeStamp deadline(deadlineTs, _anchor.domain(), _anchor.offset());

        // Read the current time once and decide what to do based on
        // the slack.  Reading the clock can fail (e.g. a synthetic
        // clock whose owner went away); propagate via result.error
        // and leave the verdict at OnTime so the caller is not
        // confused about why it isn't being told to drop.
        Result<MediaTimeStamp> nowR = _clock->now();
        if (nowR.second().isError()) {
                result.error = nowR.second();
                return result;
        }

        const int64_t deadlineNs = deadline.timeStamp().value().time_since_epoch().count();
        const int64_t nowNs = nowR.first().timeStamp().value().time_since_epoch().count();
        const int64_t slackNs = deadlineNs - nowNs;
        result.slack = Duration::fromNanoseconds(slackNs);

        if (slackNs >= 0) {
                // Deadline is in the future — sleep until it.
                Error sleepErr = _clock->sleepUntil(deadline);
                if (sleepErr.isError()) {
                        result.error = sleepErr;
                        return result;
                }
                result.verdict = PacingVerdict::OnTime;
                ++_ticksOnTime;
                return result;
        }

        // Deadline already passed.  How far behind are we?
        const int64_t lagNs = -slackNs;

        // Reanchor first — if the gap is genuinely unrecoverable the
        // caller probably wants to know via the verdict rather than
        // get a string of Skips that would each "drop" against an
        // already-stale timeline.
        if (!_reanchorThreshold.isZero() && lagNs >= _reanchorThreshold.nanoseconds()) {
                _anchor = nowR.first();
                _accumulated = Duration();
                result.verdict = PacingVerdict::Reanchor;
                ++_reanchors;
                return result;
        }

        if (!_skipThreshold.isZero() && lagNs >= _skipThreshold.nanoseconds()) {
                // Skip — recommend the caller drop this work item.
                // Report how many full periods we're behind so audio
                // backends with a cross-fade / SRC can collapse the
                // gap in one step instead of dropping per call.
                const int64_t periodNs = _period.nanoseconds();
                const int     skipped =
                        periodNs > 0 ? static_cast<int>(lagNs / periodNs) : 0;
                result.verdict = PacingVerdict::Skip;
                result.skippedTicks = skipped;
                ++_ticksSkipped;
                return result;
        }

        // Late but within the skip budget — proceed without sleeping.
        result.verdict = PacingVerdict::Late;
        ++_ticksLate;
        return result;
}

bool PacingGate::tryAcquire(const Duration &advance) {
        if (!_clock.isValid()) return true;

        if (!_armed) {
                Result<MediaTimeStamp> nowR = _clock->now();
                if (nowR.second().isError()) return false;
                _anchor = nowR.first();
                _accumulated = Duration();
                _armed = true;
                ++_ticksOnTime;
                return true;
        }

        // Probe deadline = anchor + accumulated + advance, *without*
        // advancing the stored accumulated yet — early arrivals must
        // not move the timeline forward, otherwise back-to-back early
        // calls would silently slip past the deadline.
        TimeStamp      probeTs = _anchor.timeStamp() + _accumulated + advance;
        MediaTimeStamp deadline(probeTs, _anchor.domain(), _anchor.offset());

        Result<MediaTimeStamp> nowR = _clock->now();
        if (nowR.second().isError()) return false;

        const int64_t deadlineNs = deadline.timeStamp().value().time_since_epoch().count();
        const int64_t nowNs = nowR.first().timeStamp().value().time_since_epoch().count();

        if (nowNs < deadlineNs) {
                // Too early — drop.  Timeline unchanged.
                ++_tryAcquireRejected;
                return false;
        }

        // Time has arrived (or passed) — commit the advance.
        _accumulated = _accumulated + advance;

        // Catch-up: if the lag past the new deadline exceeds the
        // reanchor threshold (e.g. upstream was paused for several
        // periods), re-anchor on now so subsequent ticks march from
        // here rather than racing through a backlog of stale deadlines.
        const int64_t lagNs = nowNs - deadlineNs;
        if (!_reanchorThreshold.isZero() && lagNs >= _reanchorThreshold.nanoseconds()) {
                _anchor = nowR.first();
                _accumulated = Duration();
                ++_reanchors;
        } else {
                ++_ticksOnTime;
        }
        return true;
}

PROMEKI_NAMESPACE_END
