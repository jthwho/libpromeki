/**
 * @file      ratetracker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ratetracker.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace promeki;

TEST_CASE("RateTracker: default state reports zero") {
        RateTracker rt;
        CHECK(rt.bytesPerSecond() == 0.0);
        CHECK(rt.framesPerSecond() == 0.0);
        CHECK(rt.windowMs() == RateTracker::kDefaultWindowMs);
}

TEST_CASE("RateTracker: invalid window clamps to >= 1 ms") {
        RateTracker rt(0);
        CHECK(rt.windowMs() == 1);
        RateTracker rt2(-10);
        CHECK(rt2.windowMs() == 1);
}

TEST_CASE("RateTracker: rate after a burst is non-zero") {
        // A short window so the test does not run for seconds.
        RateTracker rt(200);

        // Record 100 frames of 1 KiB each in a tight burst.
        for (int i = 0; i < 100; ++i) rt.record(1024);

        // With zero elapsed time the current-window reading is
        // essentially instantaneous, so we instead wait for the window
        // to roll over and query the finished-window snapshot.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // One more record forces a rotation path on the next query.
        rt.record(1024);

        double bps = rt.bytesPerSecond();
        double fps = rt.framesPerSecond();
        // 100 * 1024 = 102400 bytes in ~300ms gives ~340000 B/s.  We
        // use a generous tolerance because sleep precision and the
        // window boundary moving around make exact math impossible.
        CHECK(bps > 50000.0);
        CHECK(fps > 50.0);
}

TEST_CASE("RateTracker: reset zeroes the counters") {
        RateTracker rt(200);
        for (int i = 0; i < 50; ++i) rt.record(2048);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        rt.record(2048); // force a rotation on query
        // Confirm something was observed before resetting.
        CHECK(rt.bytesPerSecond() > 0.0);

        rt.reset();
        // Immediately after reset the tracker has no history of a
        // completed window, so both accessors must report zero.
        CHECK(rt.bytesPerSecond() == 0.0);
        CHECK(rt.framesPerSecond() == 0.0);
}

TEST_CASE("RateTracker: window rotation preserves last-window data") {
        RateTracker rt(100); // 100ms window for fast rollover

        // First window: 10 frames of 512 bytes.
        for (int i = 0; i < 10; ++i) rt.record(512);

        // Let the window age past its nominal length.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Query rotates the window; the reading should still reflect
        // the just-completed window, not zero.
        double bps = rt.bytesPerSecond();
        double fps = rt.framesPerSecond();
        CHECK(bps > 0.0);
        CHECK(fps > 0.0);
}

TEST_CASE("RateTracker: concurrent record is safe") {
        // Smoke test: stress the lock-free record() path from multiple
        // threads and confirm no crashes / hangs and that the totals
        // match the sum of per-thread contributions after a rotation.
        RateTracker rt(100);

        constexpr int            kThreads = 4;
        constexpr int            kPerThread = 1000;
        std::atomic<int>         ready{0};
        std::vector<std::thread> workers;
        workers.reserve(kThreads);

        for (int t = 0; t < kThreads; ++t) {
                workers.emplace_back([&]() {
                        ready.fetch_add(1);
                        while (ready.load() < kThreads) {}
                        for (int i = 0; i < kPerThread; ++i) {
                                rt.record(100);
                        }
                });
        }
        for (auto &w : workers) w.join();

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        rt.record(100); // force rotation

        // We recorded 4 * 1000 = 4000 frames totalling 400 000 bytes
        // in the first window, plus one more for the rotation trigger.
        // The finished-window snapshot is what bytesPerSecond should
        // surface because the new window is still young.  We only
        // assert the rate is positive; exact values depend on timing.
        CHECK(rt.bytesPerSecond() > 0.0);
        CHECK(rt.framesPerSecond() > 0.0);
}
