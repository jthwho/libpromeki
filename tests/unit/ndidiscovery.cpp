/**
 * @file      ndidiscovery.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/ndidiscovery.h>
#include <promeki/ndilib.h>

using namespace promeki;

TEST_CASE("NdiDiscovery: instance() returns the same singleton each call") {
        NdiDiscovery &a = NdiDiscovery::instance();
        NdiDiscovery &b = NdiDiscovery::instance();
        CHECK(&a == &b);
}

TEST_CASE("NdiDiscovery: worker starts when the NDI runtime is loaded") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; discovery worker is correctly disabled");
                CHECK_FALSE(disc.isRunning());
                CHECK(disc.uptimeMs() == 0);
                return;
        }
        // First instance() may be us — give the worker a brief moment
        // to call find_create_v2 and flip the running flag.  The
        // singleton is a Meyers static so this test may be the first
        // to ever trigger it across the unittest binary's lifetime.
        for (int i = 0; i < 50 && !disc.isRunning(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(disc.isRunning());
}

TEST_CASE("NdiDiscovery: sources() is callable and returns a (possibly empty) list") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        if (!NdiLib::instance().isLoaded()) return;
        // No external sources required — an empty list is a valid
        // answer when nothing is on the network.
        auto first = disc.sources();
        // Same call again should be cheap and consistent (nothing
        // disappears just because we look twice).
        auto second = disc.sources();
        CHECK(second.size() >= first.size());
}

TEST_CASE("NdiDiscovery: sources(minUptimeMs) blocks at most the requested window") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        if (!NdiLib::instance().isLoaded()) return;
        // Even if the worker has been up for a while, requesting
        // minUptimeMs past now should bound the wait.
        auto t0 = std::chrono::steady_clock::now();
        auto records = disc.sources(0);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        // sources(0) is the no-wait path — should return effectively
        // instantly.  Allow a generous 100ms ceiling for CI noise.
        CHECK(ms < 100);
        // The list call itself should not have invented entries.
        CHECK(records.size() == disc.sources().size());
}

TEST_CASE("NdiDiscovery: setPollIntervalMs clamps to a sane window") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        disc.setPollIntervalMs(50);
        CHECK(disc.pollIntervalMs() == 50);
        disc.setPollIntervalMs(10);
        // Below floor — clamps to 50.
        CHECK(disc.pollIntervalMs() == 50);
        disc.setPollIntervalMs(500);
        CHECK(disc.pollIntervalMs() == 500);
        // Restore default for any later tests.
        disc.setPollIntervalMs(500);
}

TEST_CASE("NdiDiscovery: waitForSource(name, timeout) returns empty when nothing matches") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        if (!NdiLib::instance().isLoaded()) return;
        // A canonical name that's almost certainly not on the
        // network during a unit-test run.
        auto   t0    = std::chrono::steady_clock::now();
        String found = disc.waitForSource("PROMEKI-UNITTEST-NEVER-EXISTS (sentinel)", 100);
        auto   t1    = std::chrono::steady_clock::now();
        auto   ms    = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        CHECK(found.isEmpty());
        // Should have honored the timeout and not waited materially
        // longer.  Generous slack for CI / scheduler noise.
        CHECK(ms < 500);
}

TEST_CASE("NdiDiscovery: waitForSource source-only pattern returns empty when no machine matches") {
        NdiDiscovery &disc = NdiDiscovery::instance();
        if (!NdiLib::instance().isLoaded()) return;
        // No `(` in the pattern → source-only match across any
        // machine.  Tail is intentionally absurd so it never matches
        // a real source on the test network.
        auto   t0    = std::chrono::steady_clock::now();
        String found = disc.waitForSource("promeki-unittest-never-exists-sentinel", 100);
        auto   t1    = std::chrono::steady_clock::now();
        auto   ms    = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        CHECK(found.isEmpty());
        CHECK(ms < 500);
}

#endif // PROMEKI_ENABLE_NDI
