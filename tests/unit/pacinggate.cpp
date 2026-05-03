/**
 * @file      pacinggate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for the PacingGate scheduling helper.  Drives every verdict
 * (OnTime / Late / Skip / Reanchor) plus error propagation, threshold
 * configuration, telemetry, and variable-advance pacing through a
 * deterministic FakeClock whose raw() returns whatever the test sets.
 */

#include <atomic>
#include <doctest/doctest.h>

#include <promeki/clock.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/pacinggate.h>

using namespace promeki;

namespace {

        // Deterministic Clock whose raw() returns whatever the test
        // sets.  setNow() is the one knob: it advances the clock to
        // an exact ns count.  sleepUntilNs is a no-op so wait() does
        // not block — the gate's verdicts depend only on the raw
        // now/deadline math.
        class FakeClock : public Clock {
                public:
                        FakeClock() : Clock(ClockDomain(ClockDomain::Synthetic)) {}

                        void setNow(int64_t ns) { _rawNs.store(ns, std::memory_order_relaxed); }
                        void advanceNow(int64_t ns) {
                                _rawNs.fetch_add(ns, std::memory_order_relaxed);
                        }
                        void setRawError(Error e) { _rawError = e; }
                        int  sleepCalls() const { return _sleepCalls; }

                        int64_t     resolutionNs() const override { return 1; }
                        ClockJitter jitter() const override { return ClockJitter{Duration(), Duration()}; }

                protected:
                        Result<int64_t> raw() const override {
                                if (_rawError.isError()) {
                                        return makeError<int64_t>(_rawError);
                                }
                                return makeResult<int64_t>(_rawNs.load(std::memory_order_relaxed));
                        }

                        Error sleepUntilNs(int64_t) const override {
                                ++_sleepCalls;
                                return Error::Ok;
                        }

                private:
                        std::atomic<int64_t> _rawNs{0};
                        Error                _rawError;
                        mutable int          _sleepCalls = 0;
        };

        Clock::Ptr makeFakeClock() { return Clock::Ptr::takeOwnership(new FakeClock()); }

        FakeClock *fakeOf(const Clock::Ptr &p) {
                // Const cast for tests only — we own the clock and need
                // mutable access to drive it.  Production code never
                // does this; the gate itself only reads.
                return const_cast<FakeClock *>(static_cast<const FakeClock *>(p.ptr()));
        }

        // Convenient period for most tests: a 30-fps frame interval
        // (33,333,333 ns).
        const Duration kPeriod = Duration::fromNanoseconds(33'333'333);

} // namespace

TEST_SUITE("PacingGate") {

        TEST_CASE("default-constructed gate has no clock and is not armed") {
                PacingGate gate;
                CHECK_FALSE(gate.hasClock());
                CHECK_FALSE(gate.isArmed());
                CHECK(gate.period().isZero());
        }

        TEST_CASE("no clock: every wait returns OnTime + zero slack and does nothing") {
                PacingGate   gate;
                PacingResult r = gate.wait(kPeriod);
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.slack.isZero());
                CHECK(r.error == Error::Ok);
                CHECK(r.skippedTicks == 0);
                // Ticks counters do not advance for the null-clock no-op
                // path — there's no scheduling event to count.
                CHECK(gate.ticksOnTime() == 0);
                CHECK_FALSE(gate.isArmed());
        }

        TEST_CASE("first wait after binding arms the anchor and returns OnTime") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(1'000);

                PacingGate gate(clock, kPeriod);
                CHECK_FALSE(gate.isArmed());

                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.error == Error::Ok);
                CHECK(r.slack.isZero());
                CHECK(gate.isArmed());
                CHECK(gate.ticksOnTime() == 1);
                CHECK(gate.accumulated().isZero());
                // No sleep on the arming call — the first frame ships
                // immediately.
                CHECK(fakeOf(clock)->sleepCalls() == 0);
        }

        TEST_CASE("subsequent wait sleeps when deadline is in the future") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(1'000);

                PacingGate gate(clock, kPeriod);
                gate.wait(); // arm

                // No clock advance — the next wait's deadline is
                // anchor + period (in the future).
                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.slack.nanoseconds() == kPeriod.nanoseconds());
                CHECK(fakeOf(clock)->sleepCalls() == 1);
                CHECK(gate.ticksOnTime() == 2);
        }

        TEST_CASE("Late verdict: past deadline by less than skipThreshold") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(1'000);

                PacingGate gate(clock, kPeriod);
                gate.wait(); // arm

                // Advance the clock past the next deadline by half a
                // period — well under the default skipThreshold of one
                // period.
                fakeOf(clock)->advanceNow(kPeriod.nanoseconds() + kPeriod.nanoseconds() / 2);
                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::Late);
                CHECK(r.slack.nanoseconds() < 0);
                CHECK(r.skippedTicks == 0);
                CHECK(fakeOf(clock)->sleepCalls() == 0);
                CHECK(gate.ticksLate() == 1);
                CHECK(gate.ticksSkipped() == 0);
        }

        TEST_CASE("Skip verdict: past deadline by at least one full period") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);

                PacingGate gate(clock, kPeriod);
                gate.wait(); // arm

                // Jump 3 periods forward.  Deadline was anchor + period
                // (1 period); we're now at 3 periods, so lag = 2 periods.
                // Skip threshold = 1 period (default).  Reanchor
                // threshold = 8 periods (default).  Verdict = Skip.
                fakeOf(clock)->setNow(3 * kPeriod.nanoseconds());
                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::Skip);
                CHECK(r.skippedTicks == 2);
                CHECK(r.slack.nanoseconds() == -2 * kPeriod.nanoseconds());
                CHECK(fakeOf(clock)->sleepCalls() == 0);
                CHECK(gate.ticksSkipped() == 1);
        }

        TEST_CASE("Reanchor verdict: past deadline by reanchorThreshold or more") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);

                PacingGate gate(clock, kPeriod);
                gate.wait(); // arm

                // Jump 10 periods forward — past the default reanchor
                // threshold (8 periods).
                const int64_t farNs = 10 * kPeriod.nanoseconds();
                fakeOf(clock)->setNow(farNs);
                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::Reanchor);
                CHECK(gate.reanchors() == 1);
                CHECK(gate.ticksSkipped() == 0);

                // After reanchor, the next wait should be OnTime
                // against the new anchor — accumulated reset to 0.
                CHECK(gate.accumulated().isZero());
                PacingResult r2 = gate.wait();
                CHECK(r2.verdict == PacingVerdict::OnTime);
                // anchor was reset to farNs; deadline = farNs + period.
                // sleepUntil ran since now (still farNs) is before
                // deadline.
                CHECK(fakeOf(clock)->sleepCalls() == 1);
        }

        TEST_CASE("setClock rebinds and re-arms") {
                Clock::Ptr c1 = makeFakeClock();
                fakeOf(c1)->setNow(1'000);
                PacingGate gate(c1, kPeriod);
                gate.wait();
                CHECK(gate.isArmed());

                Clock::Ptr c2 = makeFakeClock();
                fakeOf(c2)->setNow(50'000);
                gate.setClock(c2);
                CHECK_FALSE(gate.isArmed());
                CHECK(gate.clock().ptr() == c2.ptr());

                PacingResult r = gate.wait();
                // First call after setClock arms against c2's now.
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(gate.isArmed());
        }

        TEST_CASE("setClock(null) disables pacing") {
                Clock::Ptr clock = makeFakeClock();
                PacingGate gate(clock, kPeriod);
                gate.wait();

                gate.setClock(Clock::Ptr());
                CHECK_FALSE(gate.hasClock());

                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.slack.isZero());
                CHECK(r.error == Error::Ok);
        }

        TEST_CASE("rearm forces a fresh anchor on the next wait") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(1'000);
                PacingGate gate(clock, kPeriod);

                gate.wait(); // arm against now=1000
                CHECK(gate.isArmed());

                // Move the clock and rearm — the next wait should
                // anchor against the new now, not sleep against the
                // old anchor.
                fakeOf(clock)->setNow(99'999'999);
                gate.rearm();
                CHECK_FALSE(gate.isArmed());

                PacingResult r = gate.wait();
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.slack.isZero());
                CHECK(gate.isArmed());
                // Anchor latched to 99,999,999; the next wait should
                // sleep against 99,999,999 + period.
                fakeOf(clock)->setNow(99'999'999);
                PacingResult r2 = gate.wait();
                CHECK(r2.verdict == PacingVerdict::OnTime);
                CHECK(r2.slack.nanoseconds() == kPeriod.nanoseconds());
        }

        TEST_CASE("setPeriod updates default thresholds") {
                PacingGate gate;
                gate.setPeriod(Duration::fromMilliseconds(10));
                CHECK(gate.period().nanoseconds() == 10'000'000);
                CHECK(gate.skipThreshold().nanoseconds() == 10'000'000);
                CHECK(gate.reanchorThreshold().nanoseconds() ==
                      10'000'000 * PacingGate::DefaultReanchorMultiple);

                gate.setPeriod(Duration::fromMilliseconds(20));
                CHECK(gate.skipThreshold().nanoseconds() == 20'000'000);
                CHECK(gate.reanchorThreshold().nanoseconds() ==
                      20'000'000 * PacingGate::DefaultReanchorMultiple);
        }

        TEST_CASE("explicit setSkipThreshold survives setPeriod") {
                PacingGate gate;
                gate.setPeriod(Duration::fromMilliseconds(10));
                gate.setSkipThreshold(Duration::fromMilliseconds(100));
                CHECK(gate.skipThreshold().nanoseconds() == 100'000'000);

                // Period change must not clobber the explicitly-set
                // skip threshold.
                gate.setPeriod(Duration::fromMilliseconds(20));
                CHECK(gate.skipThreshold().nanoseconds() == 100'000'000);
                // Reanchor threshold was never overridden, so it still
                // tracks the new period.
                CHECK(gate.reanchorThreshold().nanoseconds() ==
                      20'000'000 * PacingGate::DefaultReanchorMultiple);
        }

        TEST_CASE("explicit setReanchorThreshold survives setPeriod") {
                PacingGate gate;
                gate.setPeriod(Duration::fromMilliseconds(10));
                gate.setReanchorThreshold(Duration::fromSeconds(5));

                gate.setPeriod(Duration::fromMilliseconds(20));
                CHECK(gate.reanchorThreshold().nanoseconds() == 5'000'000'000LL);
                CHECK(gate.skipThreshold().nanoseconds() == 20'000'000);
        }

        TEST_CASE("clock now() error during arming propagates via result.error") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setRawError(Error::ObjectGone);

                PacingGate   gate(clock, kPeriod);
                PacingResult r = gate.wait();
                CHECK(r.error == Error::ObjectGone);
                CHECK_FALSE(gate.isArmed());
                // No counter advances on error.
                CHECK(gate.ticksOnTime() == 0);
        }

        TEST_CASE("clock now() error during normal wait propagates via result.error") {
                Clock::Ptr clock = makeFakeClock();
                PacingGate gate(clock, kPeriod);
                gate.wait(); // arm cleanly

                fakeOf(clock)->setRawError(Error::ObjectGone);
                PacingResult r = gate.wait();
                CHECK(r.error == Error::ObjectGone);
                // The gate's accumulated still advanced — the timeline
                // moves forward even when the clock briefly hiccups.
                CHECK_FALSE(gate.accumulated().isZero());
        }

        TEST_CASE("variable-advance wait accumulates the supplied durations") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);

                PacingGate gate(clock); // no period — caller passes per-call
                gate.wait(Duration::fromMilliseconds(5)); // arm; advance ignored
                CHECK(gate.accumulated().isZero());

                // Advance clock by 5ms; first paced wait should be OnTime.
                fakeOf(clock)->setNow(5'000'000);
                PacingResult r = gate.wait(Duration::fromMilliseconds(5));
                CHECK(r.verdict == PacingVerdict::OnTime);
                CHECK(r.slack.isZero());
                CHECK(gate.accumulated().nanoseconds() == 5'000'000);

                // Pass a different advance — accumulated should track.
                fakeOf(clock)->setNow(8'000'000);
                PacingResult r2 = gate.wait(Duration::fromMilliseconds(3));
                CHECK(r2.verdict == PacingVerdict::OnTime);
                CHECK(gate.accumulated().nanoseconds() == 8'000'000);
        }

        TEST_CASE("tryAcquire: no clock = always due, timeline does not advance") {
                PacingGate gate;
                CHECK(gate.tryAcquire(kPeriod));
                CHECK(gate.tryAcquire(kPeriod));
                CHECK(gate.accumulated().isZero());
                CHECK(gate.tryAcquireRejected() == 0);
        }

        TEST_CASE("tryAcquire: first call after binding latches anchor and returns true") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(1'000);
                PacingGate gate(clock, kPeriod);
                CHECK(gate.tryAcquire());
                CHECK(gate.isArmed());
                CHECK(gate.accumulated().isZero());
        }

        TEST_CASE("tryAcquire: rejects early arrival without advancing timeline") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);
                PacingGate gate(clock, kPeriod);
                CHECK(gate.tryAcquire()); // arm

                // Clock barely advances — well before next deadline.
                fakeOf(clock)->setNow(kPeriod.nanoseconds() / 4);
                CHECK_FALSE(gate.tryAcquire());
                CHECK(gate.accumulated().isZero());
                CHECK(gate.tryAcquireRejected() == 1);

                // Try again at the deadline boundary — must accept.
                fakeOf(clock)->setNow(kPeriod.nanoseconds());
                CHECK(gate.tryAcquire());
                CHECK(gate.accumulated().nanoseconds() == kPeriod.nanoseconds());
                CHECK(gate.tryAcquireRejected() == 1);
        }

        TEST_CASE("tryAcquire: accepts past-deadline arrival and advances timeline") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);
                PacingGate gate(clock, kPeriod);
                gate.tryAcquire(); // arm

                fakeOf(clock)->setNow(kPeriod.nanoseconds() + kPeriod.nanoseconds() / 2);
                CHECK(gate.tryAcquire());
                CHECK(gate.accumulated().nanoseconds() == kPeriod.nanoseconds());
        }

        TEST_CASE("tryAcquire: re-anchors when lag exceeds reanchorThreshold") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);
                PacingGate gate(clock, kPeriod);
                gate.tryAcquire(); // arm

                // Jump 10 periods forward — past default reanchor
                // threshold (8 periods).
                fakeOf(clock)->setNow(10 * kPeriod.nanoseconds());
                CHECK(gate.tryAcquire());
                CHECK(gate.reanchors() == 1);
                CHECK(gate.accumulated().isZero());
        }

        TEST_CASE("telemetry counters segment by verdict") {
                Clock::Ptr clock = makeFakeClock();
                fakeOf(clock)->setNow(0);
                PacingGate gate(clock, kPeriod);

                gate.wait(); // arm = OnTime
                gate.wait(); // future deadline = OnTime sleep
                CHECK(gate.ticksOnTime() == 2);

                // Half-period lag = Late
                fakeOf(clock)->setNow(2 * kPeriod.nanoseconds() + kPeriod.nanoseconds() / 2);
                gate.wait();
                CHECK(gate.ticksLate() == 1);

                // Two-period lag = Skip
                fakeOf(clock)->setNow(6 * kPeriod.nanoseconds());
                gate.wait();
                CHECK(gate.ticksSkipped() == 1);

                // Far-past lag = Reanchor
                fakeOf(clock)->setNow(100 * kPeriod.nanoseconds());
                gate.wait();
                CHECK(gate.reanchors() == 1);
        }
}
