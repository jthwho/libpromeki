/**
 * @file      ntv2clock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/framerate.h>
#include <promeki/mediatimestamp.h>
#include <promeki/ntv2clock.h>
#include <promeki/result.h>
#include <promeki/timestamp.h>
#include <promeki/uniqueptr.h>

using namespace promeki;

namespace {

        // Trivial counter source — returns whatever value the test
        // most recently stored in *ctx.  Drives the wrap-extension
        // path in raw() without needing a real CNTV2Card.
        struct CounterSource {
                        uint32_t value = 0;
                        bool     fail  = false;
        };

        bool fakeCounter(uint32_t *out, void *ctx) {
                auto *src = static_cast<CounterSource *>(ctx);
                if (src->fail) return false;
                *out = src->value;
                return true;
        }

} // namespace

TEST_CASE("Ntv2DeviceClock: createForTest produces a usable sample-counter clock") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:wrap-basic"), /*vbi*/ false));
        REQUIRE(clk.isValid());

        CounterSource src;
        clk->setCounterSourceForTest(fakeCounter, &src);

        // Counter at 48 kHz, value = 48000 → 1 second.
        clk->setSampleRate(48000.0f);
        src.value = 48000;
        Result<int64_t> r = clk->nowNs();
        CHECK(r.second().isOk());
        CHECK(r.first() == 1'000'000'000);

        // Counter at 96 kHz, value = 96000 → 1 second.
        clk->setSampleRate(96000.0f);
        // Resetting the source to a smaller value than last seen would
        // trigger a wrap event — keep it monotonic for this check.
        src.value = 96000 + 1;
        r         = clk->nowNs();
        CHECK(r.second().isOk());
        // Sample rate doubled, so 96001 ticks ≈ 1.000010416 s.
        CHECK(r.first() > 1'000'000'000);
}

TEST_CASE("Ntv2DeviceClock: 32→64 wrap extension handles a full counter rollover") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:wrap-extension"), /*vbi*/ false));
        REQUIRE(clk.isValid());

        CounterSource src;
        clk->setCounterSourceForTest(fakeCounter, &src);
        clk->setSampleRate(48000.0f);

        // Establish shadow at a value near the wrap.
        src.value      = 0xFFFF'FFF0u; // ~4.295e9 ticks
        Result<int64_t> r1 = clk->nowNs();
        CHECK(r1.second().isOk());

        // Advance just before wrap.
        src.value      = 0xFFFF'FFFFu;
        Result<int64_t> r2 = clk->nowNs();
        CHECK(r2.second().isOk());
        CHECK(r2.first() >= r1.first());

        // Now wrap to a small value — the high word should advance.
        src.value      = 32u;
        Result<int64_t> r3 = clk->nowNs();
        CHECK(r3.second().isOk());
        // After wrap, totalTicks ≈ 2^32 + 32 ≈ 4.295e9 ticks ≈ 89486 seconds at 48 kHz.
        // r3 should be larger than r2, not smaller (which is the bug
        // we're guarding against).
        CHECK(r3.first() > r2.first());

        // The reported delta between r2 and r3 should be approximately
        // one wrap (2^32 + 32 - 0xFFFFFFFF) ticks * 1e9 / 48000.
        const int64_t deltaTicks    = (int64_t(1) << 32) + 32 - int64_t(0xFFFF'FFFFu);
        const int64_t expectedDelta = static_cast<int64_t>(static_cast<double>(deltaTicks) *
                                                           1.0e9 / 48000.0);
        const int64_t actualDelta   = r3.first() - r2.first();
        // Allow 1 µs slack for the double-precision conversion.
        CHECK(actualDelta >= expectedDelta - 1000);
        CHECK(actualDelta <= expectedDelta + 1000);
}

TEST_CASE("Ntv2DeviceClock: counter-source failure surfaces as Error::DeviceError") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:fail"), /*vbi*/ false));
        REQUIRE(clk.isValid());

        CounterSource src;
        src.fail = true;
        clk->setCounterSourceForTest(fakeCounter, &src);
        clk->setSampleRate(48000.0f);

        Result<int64_t> r = clk->nowNs();
        CHECK(r.second().isError());
        CHECK(r.second() == Error::DeviceError);
}

TEST_CASE("Ntv2DeviceClock: mediaTimeStampFromSamples maps FRAME_STAMP counts to clock domain") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:samples-to-stamp"), /*vbi*/ false));
        REQUIRE(clk.isValid());
        clk->setSampleRate(48000.0f);

        // Zero ticks → zero ns; stamp carries the clock's domain.
        const MediaTimeStamp zero = clk->mediaTimeStampFromSamples(0);
        CHECK(zero.timeStamp().nanoseconds() == 0);
        CHECK(zero.domain() == clk->domain());

        // 48,000 ticks at 48 kHz = exactly 1 second.
        const MediaTimeStamp oneSec = clk->mediaTimeStampFromSamples(48'000);
        CHECK(oneSec.timeStamp().nanoseconds() == 1'000'000'000);
        CHECK(oneSec.domain() == clk->domain());

        // 96 kHz sample rate halves the per-tick duration → 48,000 ticks
        // is half a second.
        clk->setSampleRate(96'000.0f);
        const MediaTimeStamp halfSec = clk->mediaTimeStampFromSamples(48'000);
        CHECK(halfSec.timeStamp().nanoseconds() == 500'000'000);

        // Counter value above 32 bits (driver pre-extended).  Pure
        // arithmetic check — no wrap shadow is consulted.
        clk->setSampleRate(48'000.0f);
        const uint64_t big = (uint64_t(1) << 33) + 480; // 2 wraps + 10 ms
        const MediaTimeStamp bigStamp = clk->mediaTimeStampFromSamples(big);
        const int64_t expectedNs = static_cast<int64_t>(
                static_cast<double>(big) * (1.0e9 / 48'000.0));
        CHECK(bigStamp.timeStamp().nanoseconds() == expectedNs);
}

TEST_CASE("Ntv2DeviceClock: rateRatio defaults to 1.0 before the baseline window stabilises") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:rate-default"), /*vbi*/ false));
        REQUIRE(clk.isValid());

        CounterSource src;
        clk->setCounterSourceForTest(fakeCounter, &src);
        clk->setSampleRate(48000.0f);

        // No reads yet — rateRatio reports the safe default.
        CHECK(clk->rateRatio() == 1.0);

        // A handful of reads under real wall time stay well inside
        // the 5-second baseline window so the publisher hasn't
        // produced an estimate yet.
        for (int i = 0; i < 5; ++i) {
                src.value = static_cast<uint32_t>(48000 * (i + 1));
                Result<int64_t> r = clk->nowNs();
                CHECK(r.second().isOk());
        }
        CHECK(clk->rateRatio() == 1.0);
}

TEST_CASE("Ntv2DeviceClock: rateRatio tracks counter vs wall drift over a long window") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:rate-drift"), /*vbi*/ false));
        REQUIRE(clk.isValid());

        // Inject synthetic counter + wall sources so the test can
        // simulate seconds of advance without sleeping.  The fake
        // wall starts at 0 and is advanced by the test between reads.
        CounterSource counterSrc;
        clk->setCounterSourceForTest(fakeCounter, &counterSrc);
        clk->setSampleRate(48000.0f);

        struct WallSource {
                        int64_t value = 0;
        } wall;
        clk->setWallTimeSourceForTest(
                [](void *ctx) -> int64_t {
                        return static_cast<WallSource *>(ctx)->value;
                },
                &wall);

        // Baseline read: wall and counter both at zero — rate stays 1.0.
        wall.value      = 0;
        counterSrc.value = 0;
        REQUIRE(clk->nowNs().second().isOk());
        CHECK(clk->rateRatio() == 1.0);

        // Advance counter "perfectly" with wall (1 second of wall,
        // 48000 ticks of counter = exactly 1 s of counter ns).
        // Window is 5 s, so the first update past kRateBaselineMinWindowNs
        // will publish a near-1.0 estimate; do a few iterations.
        for (int i = 1; i <= 8; ++i) {
                wall.value       = static_cast<int64_t>(i) * 1'000'000'000LL;
                counterSrc.value = static_cast<uint32_t>(48000 * i);
                REQUIRE(clk->nowNs().second().isOk());
        }
        // 1.0 ± tiny LPF blend artefacts.
        CHECK(clk->rateRatio() >= 0.999);
        CHECK(clk->rateRatio() <= 1.001);

        // Now ramp the counter faster than wall: 1000 ppm fast
        // (counter ticks +0.1% relative to wall).  Apply across a
        // long-enough span that the LPF converges.
        const double fastRatio = 1.001;
        for (int i = 9; i <= 200; ++i) {
                wall.value      = static_cast<int64_t>(i) * 1'000'000'000LL;
                const double ticks = 48000.0 * static_cast<double>(i) * fastRatio;
                counterSrc.value   = static_cast<uint32_t>(ticks);
                REQUIRE(clk->nowNs().second().isOk());
        }
        // After many LPF blends the published ratio should sit close
        // to the injected 1.001.  Allow generous slack — first few
        // updates blend in from the 1.0 default and don't reach the
        // signal floor until the wall-delta builds up.
        CHECK(clk->rateRatio() > 1.0005);
        CHECK(clk->rateRatio() < 1.002);
}

TEST_CASE("Ntv2DeviceClock: rateRatio stays at 1.0 in VBI-fallback mode") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:rate-vbi"), /*vbi*/ true));
        REQUIRE(clk.isValid());

        // VBI fallback never queries the counter, never updates the
        // drift estimator — the published ratio is fixed at 1.0
        // regardless of how often the clock is read.
        clk->setFrameRate(FrameRate(FrameRate::FPS_60));
        for (int i = 0; i < 20; ++i) {
                clk->noteVbi(TimeStamp::now());
                Result<int64_t> r = clk->nowNs();
                CHECK(r.second().isOk());
        }
        CHECK(clk->rateRatio() == 1.0);
}

TEST_CASE("Ntv2DeviceClock: VBI-fallback mode reports frame-period resolution") {
        UniquePtr<Ntv2DeviceClock> clk = UniquePtr<Ntv2DeviceClock>::takeOwnership(
                Ntv2DeviceClock::createForTest(String("ntv2-test:vbi"), /*vbi*/ true));
        REQUIRE(clk.isValid());

        // Without a frame-rate hint, resolution falls back to 1 ms.
        CHECK(clk->resolutionNs() == 1'000'000);

        clk->setFrameRate(FrameRate(FrameRate::FPS_60));
        // 60 fps → frame duration ≈ 16,666,666 ns.
        CHECK(clk->resolutionNs() >= 16'000'000);
        CHECK(clk->resolutionNs() <= 17'000'000);

        // noteVbi advances the clock; nowNs returns at-or-past the
        // TimeStamp we just handed in.  Capture the stamp before
        // noteVbi so the check is independent of scheduling jitter.
        const TimeStamp tickStamp = TimeStamp::now();
        clk->noteVbi(tickStamp);
        Result<int64_t> r = clk->nowNs();
        CHECK(r.second().isOk());
        CHECK(r.first() >= tickStamp.nanoseconds());
}

#endif // PROMEKI_ENABLE_NTV2
