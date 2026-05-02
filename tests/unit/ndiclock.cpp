/**
 * @file      ndiclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/framerate.h>
#include <promeki/ndiclock.h>
#include <promeki/result.h>

using namespace promeki;

TEST_CASE("NdiClock: domain() registers a stable Ndi domain") {
        const ClockDomain &a = NdiClock::domain();
        const ClockDomain &b = NdiClock::domain();
        CHECK(a.id() == b.id());
        CHECK(a.name() == String("Ndi"));
}

TEST_CASE("NdiClock: nowNs returns 0 before any timestamp is recorded") {
        NdiClock        clk;
        Result<int64_t> ns = clk.nowNs();
        CHECK(ns.second().isOk());
        CHECK(ns.first() == 0);
}

TEST_CASE("NdiClock: setLatestTimestamp converts NDI 100ns ticks to nanoseconds") {
        NdiClock clk;
        // 1234567 ticks × 100ns = 123,456,700 ns.
        clk.setLatestTimestamp(1234567);
        Result<int64_t> ns = clk.nowNs();
        CHECK(ns.second().isOk());
        CHECK(ns.first() == 123'456'700);

        // Subsequent timestamps update monotonically — but the clock
        // keeps the most recent regardless of order; the framework's
        // monotonic clamp on top of raw() handles non-monotonic
        // sources.
        clk.setLatestTimestamp(2'000'000);
        Result<int64_t> ns2 = clk.nowNs();
        CHECK(ns2.second().isOk());
        // 2,000,000 ticks × 100ns = 200,000,000 ns.
        CHECK(ns2.first() == 200'000'000);
}

TEST_CASE("NdiClock: undefined NDI timestamps are silently ignored") {
        // INT64_MAX is the SDK sentinel for "no timestamp available".
        NdiClock clk;
        clk.setLatestTimestamp(500);
        clk.setLatestTimestamp(INT64_MAX);
        Result<int64_t> ns = clk.nowNs();
        CHECK(ns.second().isOk());
        // Should still reflect the 500-tick timestamp, not the sentinel.
        CHECK(ns.first() == 50'000);
}

TEST_CASE("NdiClock: resolutionNs is 100 (NDI tick width)") {
        NdiClock clk;
        CHECK(clk.resolutionNs() == 100);
}

TEST_CASE("NdiClock: jitter envelope shrinks once a frame rate is supplied") {
        NdiClock clk;
        ClockJitter wide = clk.jitter();
        // Default envelope is wide (± 50ms).
        CHECK(wide.maxError.nanoseconds() == 50'000'000);
        CHECK(wide.minError.nanoseconds() == -50'000'000);

        clk.setFrameRate(FrameRate(FrameRate::FPS_30));
        ClockJitter narrow = clk.jitter();
        // Frame period for 30fps is ~33.367ms; half is ~16.683ms.
        // Allow some slop for rational rounding.
        CHECK(narrow.maxError.nanoseconds() < 17'000'000);
        CHECK(narrow.maxError.nanoseconds() > 16'000'000);
}

TEST_CASE("NdiClock: nowNs is monotonic across timestamp updates") {
        // The framework wraps raw() in a monotonic clamp — even if a
        // late-arriving NDI frame carries a backwards timestamp
        // (clock skew at the sender, packet reordering, etc.), the
        // managed nowNs() never decreases.  This is the contract
        // downstream consumers rely on.
        NdiClock clk;
        clk.setLatestTimestamp(50'000);
        CHECK(clk.nowNs().first() == 5'000'000);
        // Backwards step: raw is 4'000'000 but the clamp keeps the
        // managed value at the last seen high-water mark.
        clk.setLatestTimestamp(40'000);
        CHECK(clk.nowNs().first() == 5'000'000);
        // Forward step crosses the high-water mark cleanly.
        clk.setLatestTimestamp(60'000);
        CHECK(clk.nowNs().first() == 6'000'000);
}

#endif // PROMEKI_ENABLE_NDI
