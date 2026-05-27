/**
 * @file      cadence.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/cadence.h>
#include <promeki/duration.h>
#include <promeki/timestamp.h>

using namespace promeki;

TEST_CASE("Cadence: anchor + first next returns t0") {
        Cadence       c(Duration::fromMilliseconds(1));
        const TimeStamp t0 = TimeStamp::now();
        c.anchor(t0);
        CHECK(c.next() == t0);
}

TEST_CASE("Cadence: consecutive next() spaced by interval") {
        const Duration interval = Duration::fromMicroseconds(1000);
        Cadence       c(interval);
        const TimeStamp t0 = TimeStamp::now();
        c.anchor(t0);
        const TimeStamp d0 = c.next();
        const TimeStamp d1 = c.next();
        const TimeStamp d2 = c.next();
        CHECK(d0 == t0);
        CHECK(d1 == t0 + interval);
        CHECK(d2 == t0 + interval + interval);
}

TEST_CASE("Cadence: next() is monotone non-decreasing for positive interval") {
        Cadence       c(Duration::fromMicroseconds(500));
        const TimeStamp t0 = TimeStamp::now();
        c.anchor(t0);
        TimeStamp prev = c.next();
        for (int i = 0; i < 1000; ++i) {
                TimeStamp cur = c.next();
                // TimeStamp lacks ordering operators; compare via
                // the underlying chrono value (steady_clock).
                CHECK(cur.value() >= prev.value());
                prev = cur;
        }
}

TEST_CASE("Cadence: 10k ticks accumulate exactly N * interval (no drift)") {
        // The whole point of anchored deadlines is that drift does
        // not accumulate.  Verify the cursor after 10000 ticks
        // equals t0 + 10000 * interval *exactly* — no rounding,
        // no per-call accumulation.
        const Duration interval = Duration::fromMicroseconds(125);
        Cadence       c(interval);
        const TimeStamp t0 = TimeStamp::now();
        c.anchor(t0);
        TimeStamp last = t0;
        for (int i = 0; i < 10000; ++i) last = c.next();
        // last is the deadline returned from the 10000th call —
        // i.e. t0 + 9999 * interval.
        const Duration expected = Duration::fromNanoseconds(interval.nanoseconds() * 9999);
        CHECK((last - t0).nanoseconds() == expected.nanoseconds());
}

TEST_CASE("Cadence: ticks() counts every next() call") {
        Cadence       c(Duration::fromMicroseconds(100));
        c.anchor(TimeStamp::now());
        CHECK(c.ticks() == 0u);
        c.next();
        CHECK(c.ticks() == 1u);
        c.next();
        c.next();
        CHECK(c.ticks() == 3u);
}

TEST_CASE("Cadence: reanchor jumps forward by one interval (no burst)") {
        // Simulate a long stall: anchor at t0, advance a few
        // ticks, then reanchor at "much later".  The next deadline
        // must be at reanchorTime + interval — *not* at the
        // accumulated cursor position (which is still near t0) and
        // *not* at reanchorTime itself (which would burst-emit
        // back-to-back with whatever fired last).
        const Duration interval = Duration::fromMicroseconds(1000);
        Cadence       c(interval);
        const TimeStamp t0 = TimeStamp::now();
        c.anchor(t0);
        c.next();  // returns t0
        c.next();  // returns t0 + interval
        // 50 ms later, signal recovery from a stall.
        const TimeStamp recoveryAt = t0 + Duration::fromMilliseconds(50);
        c.reanchor(recoveryAt);
        CHECK(c.next() == recoveryAt + interval);
}

TEST_CASE("Cadence: ticks() is monotone across reanchor") {
        // Per-stream stats (e.g. silenceSamplesEmitted) sample
        // ticks() — monotonicity guarantees stats stay sane across
        // a stall recovery.
        Cadence       c(Duration::fromMicroseconds(1000));
        c.anchor(TimeStamp::now());
        c.next();
        c.next();
        c.next();
        const uint64_t before = c.ticks();
        CHECK(before == 3u);
        c.reanchor(TimeStamp::now() + Duration::fromMilliseconds(100));
        // reanchor does NOT reset ticks.
        CHECK(c.ticks() == before);
        c.next();
        CHECK(c.ticks() == before + 1u);
}

TEST_CASE("Cadence: anchor() resets ticks back to zero") {
        Cadence c(Duration::fromMicroseconds(1000));
        c.anchor(TimeStamp::now());
        c.next();
        c.next();
        CHECK(c.ticks() == 2u);
        // Re-anchoring with anchor() (not reanchor()) resets the
        // counter — used at start-of-stream / after explicit
        // restart.
        c.anchor(TimeStamp::now());
        CHECK(c.ticks() == 0u);
}

TEST_CASE("Cadence: interval() round-trips") {
        const Duration interval = Duration::fromMicroseconds(2500);
        Cadence       c(interval);
        CHECK(c.interval().nanoseconds() == interval.nanoseconds());
}
