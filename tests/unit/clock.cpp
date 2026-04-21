/**
 * @file      clock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/syntheticclock.h>

#include <atomic>

using namespace promeki;

namespace {

// Deterministic Clock whose raw() returns whatever the test sets.
// Used to exercise the base-class machinery (filter, offset, pause,
// monotonic clamp, error propagation) without depending on wall time
// or a real hardware source.
class FakeClock : public Clock {
        public:
                FakeClock(const Duration &fixedOffset = Duration(),
                          ClockPauseMode pauseMode = ClockPauseMode::CannotPause,
                          ClockFilter *filter = nullptr)
                        : Clock(ClockDomain(ClockDomain::Synthetic),
                                fixedOffset, pauseMode, filter)
                {}

                void setRaw(int64_t ns) {
                        _rawNs.store(ns, std::memory_order_relaxed);
                }

                void setRawError(Error err) {
                        _rawError = err;
                }

                int onPauseCalls() const { return _onPauseCalls; }
                bool lastPauseArg() const { return _lastPauseArg; }

                int64_t      resolutionNs() const override { return 1; }
                ClockJitter  jitter() const override {
                        return ClockJitter{Duration(), Duration()};
                }

        protected:
                Result<int64_t> raw() const override {
                        if(_rawError.isError()) {
                                return makeError<int64_t>(_rawError);
                        }
                        return makeResult<int64_t>(
                                _rawNs.load(std::memory_order_relaxed));
                }

                Error sleepUntilNs(int64_t) const override { return {}; }

                Error onPause(bool paused) override {
                        ++_onPauseCalls;
                        _lastPauseArg = paused;
                        return {};
                }

        private:
                std::atomic<int64_t> _rawNs{0};
                Error                _rawError;
                int                  _onPauseCalls = 0;
                bool                 _lastPauseArg = false;
};

// Simple test filter that subtracts a constant bias.  Used to prove
// the filter is applied between raw() and the offset subtraction in
// the base-class now() pipeline.
class BiasFilter : public ClockFilter {
        public:
                explicit BiasFilter(int64_t bias) : _bias(bias) {}
                int64_t filter(int64_t rawNs) override { return rawNs - _bias; }
        private:
                int64_t _bias;
};

} // namespace

TEST_SUITE("Clock") {

TEST_CASE("domain() comes from base-class constructor") {
        FakeClock c;
        CHECK(c.domain() == ClockDomain(ClockDomain::Synthetic));
}

TEST_CASE("raw value passes through when no filter or offset") {
        FakeClock c;
        c.setRaw(12345);
        auto r = c.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 12345);
}

TEST_CASE("fixed offset is subtracted from filtered raw value") {
        FakeClock c(Duration::fromNanoseconds(100));
        c.setRaw(500);
        auto r = c.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 400);
}

TEST_CASE("negative fixed offset adds to reported value") {
        FakeClock c(Duration::fromNanoseconds(-50));
        c.setRaw(500);
        auto r = c.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 550);
}

TEST_CASE("setFixedOffset updates subsequent reads") {
        FakeClock c;
        c.setRaw(1000);
        CHECK(value(c.nowNs()) == 1000);

        c.setFixedOffset(Duration::fromNanoseconds(300));
        c.setRaw(2000);
        CHECK(value(c.nowNs()) == 1700);

        CHECK(c.fixedOffset().nanoseconds() == 300);
}

TEST_CASE("monotonic clamp never lets reported time decrease") {
        FakeClock c;
        c.setRaw(1000);
        CHECK(value(c.nowNs()) == 1000);

        // Raw jumps backward; reported time must not.
        c.setRaw(500);
        CHECK(value(c.nowNs()) == 1000);

        // And it resumes forward progress correctly.
        c.setRaw(2000);
        CHECK(value(c.nowNs()) == 2000);
}

TEST_CASE("filter is applied before offset") {
        FakeClock c(Duration::fromNanoseconds(10),
                    ClockPauseMode::CannotPause,
                    new BiasFilter(100));
        c.setRaw(1000);

        // raw=1000, filter subtracts 100 -> 900, offset subtracts 10 -> 890.
        auto r = c.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 890);
}

TEST_CASE("raw() error propagates through now() and nowNs()") {
        FakeClock c;
        c.setRawError(Error::DeviceError);

        auto nn = c.nowNs();
        REQUIRE(isError(nn));
        CHECK(error(nn) == Error::DeviceError);

        auto n = c.now();
        REQUIRE(isError(n));
        CHECK(error(n) == Error::DeviceError);
}

TEST_CASE("setPause fails when clock cannot pause") {
        FakeClock c;
        Error e = c.setPause(true);
        CHECK(e == Error::NotSupported);
        CHECK(c.isPaused() == false);
}

TEST_CASE("pause with raw-keeps-running: reported time frozen then seamless") {
        FakeClock c(Duration(), ClockPauseMode::PausesRawKeepsRunning);
        c.setRaw(1000);
        CHECK(value(c.nowNs()) == 1000);

        CHECK(c.setPause(true).isOk());
        CHECK(c.isPaused());
        CHECK(c.onPauseCalls() == 1);
        CHECK(c.lastPauseArg() == true);

        // Raw keeps advancing during pause; reported stays frozen.
        c.setRaw(1500);
        CHECK(value(c.nowNs()) == 1000);
        c.setRaw(2000);
        CHECK(value(c.nowNs()) == 1000);

        // Resume — reported continues from where it was.
        CHECK(c.setPause(false).isOk());
        CHECK(!c.isPaused());
        CHECK(c.onPauseCalls() == 2);
        CHECK(value(c.nowNs()) == 1000);

        // New advances show up against the frozen baseline.
        c.setRaw(2500);
        CHECK(value(c.nowNs()) == 1500);
}

TEST_CASE("pause with raw-stops: no accumulator growth on resume") {
        FakeClock c(Duration(), ClockPauseMode::PausesRawStops);
        c.setRaw(1000);
        CHECK(value(c.nowNs()) == 1000);

        CHECK(c.setPause(true).isOk());
        // Simulate hw-paused clock: raw stays put while paused.
        CHECK(value(c.nowNs()) == 1000);

        CHECK(c.setPause(false).isOk());
        // Delta was zero so there's no offset growth.
        CHECK(value(c.nowNs()) == 1000);

        c.setRaw(1500);
        CHECK(value(c.nowNs()) == 1500);
}

TEST_CASE("sleepUntil rejects MediaTimeStamp from another domain") {
        FakeClock c;
        TimeStamp ts{TimeStamp::Clock::time_point(std::chrono::nanoseconds(0))};
        MediaTimeStamp wrongDomain(
                ts, ClockDomain(ClockDomain::SystemMonotonic));
        CHECK(c.sleepUntil(wrongDomain) == Error::ClockDomainMismatch);
}

TEST_CASE("sleepUntil accepts matching-domain MediaTimeStamp") {
        FakeClock c;
        c.setRaw(0);
        TimeStamp ts{TimeStamp::Clock::time_point(std::chrono::nanoseconds(0))};
        MediaTimeStamp matching(ts, c.domain());
        CHECK(c.sleepUntil(matching).isOk());
}

TEST_CASE("sleepUntil returns ClockPaused when clock is paused") {
        FakeClock c(Duration(), ClockPauseMode::PausesRawKeepsRunning);
        c.setRaw(0);
        TimeStamp ts{TimeStamp::Clock::time_point(std::chrono::nanoseconds(1000))};
        MediaTimeStamp deadline(ts, c.domain());

        REQUIRE(c.setPause(true).isOk());
        CHECK(c.sleepUntil(deadline) == Error::ClockPaused);

        REQUIRE(c.setPause(false).isOk());
        CHECK(c.sleepUntil(deadline).isOk());
}

TEST_CASE("now() returns MediaTimeStamp with correct domain and offset") {
        FakeClock c(Duration::fromNanoseconds(250));
        c.setRaw(1000);

        auto r = c.now();
        REQUIRE(isOk(r));
        MediaTimeStamp mts = value(r);
        CHECK(mts.domain() == ClockDomain(ClockDomain::Synthetic));
        CHECK(mts.timeStamp().nanoseconds() == 750);
        CHECK(mts.offset().nanoseconds() == 250);
}

TEST_CASE("Clock::Ptr refcounts") {
        auto p = Clock::Ptr::takeOwnership(new FakeClock());
        REQUIRE(p.isValid());
        CHECK(p.referenceCount() == 1);

        auto q = p;
        CHECK(p.referenceCount() == 2);

        q.clear();
        CHECK(p.referenceCount() == 1);
}

} // TEST_SUITE

TEST_SUITE("WallClock") {

TEST_CASE("basic properties and monotonic") {
        WallClock clock;
        CHECK(clock.domain() == ClockDomain(ClockDomain::SystemMonotonic));
        CHECK(clock.resolutionNs() == 1);
        CHECK(clock.canPause() == false);

        auto first = clock.nowNs();
        auto second = clock.nowNs();
        REQUIRE(isOk(first));
        REQUIRE(isOk(second));
        CHECK(value(first) > 0);
        CHECK(value(second) >= value(first));
}

} // TEST_SUITE

TEST_SUITE("SyntheticClock") {

TEST_CASE("frame counter drives raw time") {
        SyntheticClock clock(FrameRate(FrameRate::FPS_25));
        clock.setCurrentFrame(25);
        auto r = clock.nowNs();
        REQUIRE(isOk(r));
        // 25 frames at 25 fps = 1 s = 1e9 ns (exact, no integer rounding)
        CHECK(value(r) == 1000000000LL);
}

TEST_CASE("advance moves the counter") {
        SyntheticClock clock(FrameRate(FrameRate::FPS_25));
        clock.advance(50);
        CHECK(clock.currentFrame() == 50);
        auto r = clock.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 2000000000LL);
}

TEST_CASE("fixed offset stacks on top of frame counter") {
        SyntheticClock clock(FrameRate(FrameRate::FPS_25));
        clock.setFixedOffset(Duration::fromMilliseconds(100));
        clock.setCurrentFrame(25);
        auto r = clock.nowNs();
        REQUIRE(isOk(r));
        CHECK(value(r) == 1000000000LL - 100000000LL);
}

} // TEST_SUITE
