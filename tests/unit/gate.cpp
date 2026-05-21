/**
 * @file      gate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <vector>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/gate.h>

using namespace promeki;

// ============================================================================
// OnceGate
// ============================================================================

TEST_CASE("OnceGate_FiresExactlyOnce_SingleThread") {
        OnceGate gate;
        CHECK(gate.fire() == true);
        for (int i = 0; i < 100; ++i) CHECK(gate.fire() == false);
        CHECK(gate.hasFired() == true);
}

TEST_CASE("OnceGate_FiresExactlyOnce_Concurrent") {
        OnceGate    gate;
        Atomic<int> wins{0};

        std::vector<std::thread> threads;
        threads.reserve(32);
        for (int i = 0; i < 32; ++i) {
                threads.emplace_back([&] {
                        if (gate.fire()) wins.fetchAndAdd(1, MemoryOrder::Relaxed);
                });
        }
        for (auto &t : threads) t.join();
        CHECK(wins.value() == 1);
        CHECK(gate.hasFired() == true);
}

TEST_CASE("OnceGate_ResetReArmsTheGate") {
        OnceGate gate;
        CHECK(gate.fire() == true);
        CHECK(gate.fire() == false);
        gate.reset();
        CHECK(gate.hasFired() == false);
        CHECK(gate.fire() == true);
        CHECK(gate.fire() == false);
}

TEST_CASE("PROMEKI_ONCE_MacroRunsBodyExactlyOnce") {
        int hits = 0;
        for (int i = 0; i < 50; ++i) {
                PROMEKI_ONCE { ++hits; }
        }
        CHECK(hits == 1);
}

// ============================================================================
// ThrottleGate
// ============================================================================

TEST_CASE("ThrottleGate_FirstFireSucceeds") {
        ThrottleGate gate;
        CHECK(gate.fire(1000) == true);
}

TEST_CASE("ThrottleGate_SuccessiveFiresWithinIntervalAreSuppressed") {
        ThrottleGate gate;
        CHECK(gate.fire(10000) == true);
        for (int i = 0; i < 10; ++i) CHECK(gate.fire(10000) == false);
        CHECK(gate.suppressedCount() == 10);
}

TEST_CASE("ThrottleGate_ConsumeSuppressedReadsAndClears") {
        ThrottleGate gate;
        CHECK(gate.fire(10000) == true);
        for (int i = 0; i < 5; ++i) gate.fire(10000);
        CHECK(gate.consumeSuppressed() == 5);
        CHECK(gate.suppressedCount() == 0);
}

TEST_CASE("ThrottleGate_NonPositiveIntervalAlwaysFires") {
        ThrottleGate gate;
        for (int i = 0; i < 50; ++i) {
                CHECK(gate.fire(0) == true);
                CHECK(gate.fire(-5) == true);
        }
}

TEST_CASE("ThrottleGate_RefiresAfterIntervalElapses") {
        ThrottleGate gate;
        CHECK(gate.fire(50) == true);
        CHECK(gate.fire(50) == false);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(gate.fire(50) == true);
}

TEST_CASE("ThrottleGate_ConcurrentFiresAtMostOnePerInterval") {
        ThrottleGate gate;
        Atomic<int>  wins{0};
        Atomic<int>  losses{0};

        std::vector<std::thread> threads;
        threads.reserve(32);
        for (int i = 0; i < 32; ++i) {
                threads.emplace_back([&] {
                        if (gate.fire(10000)) wins.fetchAndAdd(1, MemoryOrder::Relaxed);
                        else losses.fetchAndAdd(1, MemoryOrder::Relaxed);
                });
        }
        for (auto &t : threads) t.join();
        CHECK(wins.value() == 1);
        CHECK(losses.value() == 31);
        CHECK(gate.suppressedCount() == 31);
}

TEST_CASE("ThrottleGate_ResetReArmsAndClears") {
        ThrottleGate gate;
        CHECK(gate.fire(10000) == true);
        for (int i = 0; i < 3; ++i) gate.fire(10000);
        gate.reset();
        CHECK(gate.suppressedCount() == 0);
        CHECK(gate.fire(10000) == true);
}

TEST_CASE("PROMEKI_THROTTLED_MacroRunsBodyAtMostOncePerInterval") {
        int hits = 0;
        for (int i = 0; i < 50; ++i) {
                PROMEKI_THROTTLED(10000) { ++hits; }
        }
        CHECK(hits == 1);
}
