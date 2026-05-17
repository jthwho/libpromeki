/**
 * @file      ntv2clock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/framerate.h>
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
