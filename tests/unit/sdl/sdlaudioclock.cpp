/**
 * @file      sdlaudioclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlaudioclock.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/audiodesc.h>
#include <promeki/timestamp.h>
#include <promeki/string.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <climits>
#include <thread>

using namespace promeki;

namespace {

// How long the smoothing test runs.  Ten seconds lets the internal
// rate filter reach its stable window (2 s) and exercises hundreds
// of simulated callback cycles.  Drop to 1000 once the behaviour
// is trusted.
constexpr int64_t kTestDurationMs = 10000;

// Simulated SDL audio callback period.  Real SDL callbacks usually
// land in the 10-20 ms range across supported backends.
constexpr int64_t kCallbackPeriodMs = 20;

// nowNs() polling cadence from the test thread.  Well below the
// callback period so interpolation samples fall between updates
// to the underlying consumed-byte counter.
constexpr int64_t kPollPeriodMicros = 500;

// 48 kHz stereo float: 384 000 bytes/sec.
constexpr float        kSampleRate = 48000.0f;
constexpr unsigned int kChannels   = 2;

/**
 * @brief Test stand-in for SDLAudioOutput.
 *
 * Overrides totalBytesPushed() and queuedBytes() with atomic
 * counters the test controls directly, so the clock can be
 * driven without opening a real SDL audio device.
 */
class TestAudioOutput : public SDLAudioOutput {
        public:
                TestAudioOutput() {
                        configure(AudioDesc(kSampleRate, kChannels));
                        _domain = ClockDomain(ClockDomain::registerDomain(
                                "sdl.audio:test",
                                "Test stand-in for SDL audio clock domain",
                                ClockEpoch::PerStream));
                }

                int64_t totalBytesPushed() const override {
                        return _pushed.load(std::memory_order_relaxed);
                }

                int queuedBytes() const override {
                        return _queued.load(std::memory_order_relaxed);
                }

                ClockDomain clockDomain() const override { return _domain; }

                Error setPaused(bool paused) override {
                        _paused.store(paused, std::memory_order_relaxed);
                        return {};
                }

                bool isPaused() const override {
                        return _paused.load(std::memory_order_relaxed);
                }

                void setPushed(int64_t v) {
                        _pushed.store(v, std::memory_order_relaxed);
                }

                void setQueued(int v) {
                        _queued.store(v, std::memory_order_relaxed);
                }

        private:
                std::atomic<int64_t> _pushed{0};
                std::atomic<int>     _queued{0};
                std::atomic<bool>    _paused{false};
                ClockDomain          _domain;
};

} // namespace

TEST_SUITE("SDLAudioClock") {

TEST_CASE("nowNs advances smoothly between simulated callbacks") {
        // 48 kHz stereo float -> 384 000 bytes/sec.
        const double bytesPerSec =
                (double)kSampleRate * kChannels * sizeof(float);

        TestAudioOutput output;
        SDLAudioClock clock(&output);

        // Pre-buffer enough audio to cover the whole test.  The
        // simulator drains queuedBytes to advance "consumed" in the
        // same chunked pattern a real SDL callback produces, while
        // totalBytesPushed stays fixed at the pre-buffer level.
        const int64_t kPrebufferBytes =
                (int64_t)(bytesPerSec * (kTestDurationMs + 1000) / 1000);
        output.setPushed(kPrebufferBytes);
        output.setQueued(static_cast<int>(kPrebufferBytes));

        // Simulator thread: every kCallbackPeriodMs, drop the queued
        // count by one period's worth of audio, making consumed step
        // forward in chunks exactly the way the real device does.
        std::atomic<bool> running{true};
        std::thread sim([&]{
                int64_t cbCount = 0;
                while(running.load(std::memory_order_relaxed)) {
                        std::this_thread::sleep_for(
                                std::chrono::milliseconds(kCallbackPeriodMs));
                        ++cbCount;
                        int64_t consumed = (int64_t)(
                                bytesPerSec * (double)cbCount *
                                ((double)kCallbackPeriodMs / 1000.0));
                        int64_t queued = kPrebufferBytes - consumed;
                        if(queued < 0) queued = 0;
                        output.setQueued(static_cast<int>(queued));
                }
        });

        // Give the simulator a head start so consumed is non-zero
        // when the first nowNs() read establishes the checkpoint.
        std::this_thread::sleep_for(
                std::chrono::milliseconds(kCallbackPeriodMs * 2));

        auto clockNow = [&clock]() {
                auto r = clock.nowNs();
                REQUIRE(isOk(r));
                return value(r);
        };

        const int64_t startWallNs  = TimeStamp::now().nanoseconds();
        const int64_t startClockNs = clockNow();
        int64_t lastClockNs   = startClockNs;
        int64_t stallStartWall = startWallNs;
        int64_t maxStallNs    = 0;
        int64_t minStepNs     = INT64_MAX;
        int64_t sumStepNs     = 0;
        int     stepCount     = 0;
        int     sampleCount   = 0;
        int     regressions   = 0;
        int64_t maxRegressionNs = 0;

        while(true) {
                std::this_thread::sleep_for(
                        std::chrono::microseconds(kPollPeriodMicros));
                int64_t wallNs = TimeStamp::now().nanoseconds();
                if((wallNs - startWallNs) / 1000000LL >= kTestDurationMs) {
                        break;
                }

                int64_t clkNs = clockNow();
                ++sampleCount;

                if(clkNs < lastClockNs) {
                        // A checkpoint resync can briefly pull the
                        // reported time backwards (documented).  Track
                        // frequency + magnitude so we can bound both.
                        ++regressions;
                        int64_t drop = lastClockNs - clkNs;
                        if(drop > maxRegressionNs) maxRegressionNs = drop;
                        lastClockNs = clkNs;
                        stallStartWall = wallNs;
                        continue;
                }

                if(clkNs == lastClockNs) {
                        int64_t stall = wallNs - stallStartWall;
                        if(stall > maxStallNs) maxStallNs = stall;
                } else {
                        int64_t step = clkNs - lastClockNs;
                        if(step < minStepNs) minStepNs = step;
                        sumStepNs += step;
                        ++stepCount;
                        lastClockNs = clkNs;
                        stallStartWall = wallNs;
                }
        }

        running.store(false, std::memory_order_relaxed);
        sim.join();

        const int64_t endWallNs  = TimeStamp::now().nanoseconds();
        const int64_t endClockNs = clockNow();

        const double wallSecs = (double)(endWallNs  - startWallNs ) / 1e9;
        const double clkSecs  = (double)(endClockNs - startClockNs) / 1e9;
        const double rateRatio = clock.rateRatio();
        const double callbackPeriodNs = (double)kCallbackPeriodMs * 1e6;

        MESSAGE("samples=" << sampleCount
                << " stepCount=" << stepCount
                << " maxStallNs=" << maxStallNs
                << " minStepNs=" << (stepCount ? minStepNs : 0)
                << " avgStepNs=" << (stepCount ? sumStepNs / stepCount : 0)
                << " regressions=" << regressions
                << " maxRegressionNs=" << maxRegressionNs
                << " wallSecs=" << wallSecs
                << " clkSecs=" << clkSecs
                << " rateRatio=" << rateRatio);

        // Clock should track wall-clock rate within a few percent.
        // The simulator drains at exactly the nominal rate, so
        // clkSecs / wallSecs should converge to 1.0.
        CHECK(wallSecs > 0.0);
        CHECK(clkSecs / wallSecs == doctest::Approx(1.0).epsilon(0.05));

        // Smoothing proof.  Without the interpolation checkpoint,
        // the reported time would sit on a staircase where each
        // step equals the callback period, so every 500 us poll
        // inside a ~20 ms tread would see the same value — stalls
        // would approach the full callback period.  With smoothing
        // the stall must stay well under that; allow up to a
        // quarter-period to absorb scheduling jitter.
        CHECK(maxStallNs <
              (int64_t)(callbackPeriodNs / 4.0));

        // We did observe forward steps — i.e. nowNs actually moved
        // during polling, not only at checkpoint resyncs.
        CHECK(stepCount > 0);

        // nowNs() must be strictly monotonic.  Downstream consumers
        // (rate converters, sleep-until loops, timestamp generators)
        // assume monotonic time and will misbehave on any backward
        // step, however small.
        CHECK(regressions == 0);
        CHECK(maxRegressionNs == 0);

        // The published rate ratio is slow-ramped, so by the end of
        // a 10 s run it should be drifting toward 1.0 from its 1.0
        // seed.  Sanity bound: stays near 1.0 throughout.
        CHECK(rateRatio == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("canPause is true and pauseMode is PausesRawStops") {
        TestAudioOutput output;
        SDLAudioClock clock(&output);
        CHECK(clock.canPause());
        // onPause(true) snapshots the current wall-interpolated raw
        // value into @ref _rawAtPause and flips a flag so raw()
        // returns it unchanged for the paused duration.  onPause(false)
        // re-anchors the interpolation checkpoint so raw() resumes
        // from that frozen value — raw truly stops, and the base's
        // paused-offset delta comes out to zero.
        CHECK(clock.pauseMode() == ClockPauseMode::PausesRawStops);
}

TEST_CASE("setPause delegates to SDLAudioOutput::setPaused") {
        const double bytesPerSec =
                (double)kSampleRate * kChannels * sizeof(float);
        TestAudioOutput output;
        SDLAudioClock clock(&output);

        // Seed a non-zero consumed value so the clock is past its
        // silent-startup state and pause/resume touches the normal
        // monotonic-interpolation path.
        output.setPushed((int64_t)bytesPerSec);
        output.setQueued(0);
        auto r0 = clock.nowNs();
        REQUIRE(isOk(r0));
        CHECK(value(r0) > 0);

        CHECK(clock.setPause(true).isOk());
        CHECK(output.isPaused());
        CHECK(clock.isPaused());

        CHECK(clock.setPause(false).isOk());
        CHECK_FALSE(output.isPaused());
        CHECK_FALSE(clock.isPaused());
}

TEST_CASE("reported time is frozen while paused, seamless on resume") {
        const double bytesPerSec =
                (double)kSampleRate * kChannels * sizeof(float);
        TestAudioOutput output;
        SDLAudioClock clock(&output);

        // One second of audio consumed.
        output.setPushed((int64_t)bytesPerSec);
        output.setQueued(0);
        REQUIRE(isOk(clock.nowNs()));

        CHECK(clock.setPause(true).isOk());

        // The pause snapshot fixes the reported time — subsequent
        // calls while paused should report the same value, even if
        // the underlying consumed counter kept moving (a hardware-
        // stopped device wouldn't advance, but the interpolation
        // layer in SDLAudioClock otherwise would).
        auto r1 = clock.nowNs();
        REQUIRE(isOk(r1));
        int64_t paused1 = value(r1);

        // Advance the fake consumed counter during the pause — under
        // ClockPauseMode::PausesRawStops the base-class delta
        // accounting must still show a flat nowNs.
        output.setPushed((int64_t)(bytesPerSec * 2));

        auto r2 = clock.nowNs();
        REQUIRE(isOk(r2));
        CHECK(value(r2) == paused1);

        CHECK(clock.setPause(false).isOk());

        // No backward jump on resume, and no forward jump past what
        // interpolation would produce in the resume-call interval
        // (SDLAudioClock's internal rate estimator continues to tick
        // wall-clock nanoseconds even when consumed is static).
        // Ten callback periods is well beyond any realistic wall-time
        // gap between setPause(false) and the subsequent read.
        auto r3 = clock.nowNs();
        REQUIRE(isOk(r3));
        CHECK(value(r3) >= paused1);
        CHECK((value(r3) - paused1) < (int64_t)kCallbackPeriodMs * 10 * 1000000LL);
}

TEST_CASE("raw returns ObjectGone after output destroyed") {
        std::unique_ptr<TestAudioOutput> output(new TestAudioOutput());
        SDLAudioClock clock(output.get());
        output.reset();

        auto r = clock.nowNs();
        REQUIRE(isError(r));
        CHECK(error(r) == Error::ObjectGone);
}

} // TEST_SUITE
