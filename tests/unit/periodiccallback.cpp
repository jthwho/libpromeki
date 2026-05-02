/**
 * @file      periodiccallback.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/periodiccallback.h>
#include <promeki/thread.h>
#include <thread>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("PeriodicCallback: default is invalid") {
        PeriodicCallback pc;
        CHECK_FALSE(pc.isValid());
        CHECK_FALSE(pc.service());
}

TEST_CASE("PeriodicCallback: constructed with interval and function is valid") {
        int              count = 0;
        PeriodicCallback pc(1.0, [&] { count++; });
        CHECK(pc.isValid());
        CHECK(pc.interval() == 1.0);
}

// ============================================================================
// Timing behavior
// ============================================================================

TEST_CASE("PeriodicCallback: first service starts the clock, does not fire") {
        int              count = 0;
        PeriodicCallback pc(0.01, [&] { count++; });
        CHECK_FALSE(pc.service());
        CHECK(count == 0);
}

TEST_CASE("PeriodicCallback: fires after interval elapses") {
        int              count = 0;
        PeriodicCallback pc(0.05, [&] { count++; });
        pc.service(); // starts clock
        Thread::sleepMs(60);
        CHECK(pc.service());
        CHECK(count == 1);
}

TEST_CASE("PeriodicCallback: does not fire before interval") {
        int              count = 0;
        PeriodicCallback pc(1.0, [&] { count++; });
        pc.service(); // starts clock
        CHECK_FALSE(pc.service());
        CHECK(count == 0);
}

TEST_CASE("PeriodicCallback: fires repeatedly") {
        int              count = 0;
        PeriodicCallback pc(0.05, [&] { count++; });
        pc.service(); // starts clock
        for (int i = 0; i < 3; i++) {
                Thread::sleepMs(60);
                pc.service();
        }
        CHECK(count == 3);
}

// ============================================================================
// Reset
// ============================================================================

TEST_CASE("PeriodicCallback: reset restarts the clock") {
        int              count = 0;
        PeriodicCallback pc(0.05, [&] { count++; });
        pc.service();
        Thread::sleepMs(60);
        pc.reset();
        // After reset, first service re-starts, should not fire
        CHECK_FALSE(pc.service());
        CHECK(count == 0);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("PeriodicCallback: setInterval changes the interval") {
        int              count = 0;
        PeriodicCallback pc(10.0, [&] { count++; });
        pc.service();
        pc.setInterval(0.05);
        Thread::sleepMs(60);
        CHECK(pc.service());
        CHECK(count == 1);
}

TEST_CASE("PeriodicCallback: setCallback changes the function") {
        int              countA = 0;
        int              countB = 0;
        PeriodicCallback pc(0.05, [&] { countA++; });
        pc.service();
        pc.setCallback([&] { countB++; });
        Thread::sleepMs(60);
        pc.service();
        CHECK(countA == 0);
        CHECK(countB == 1);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("PeriodicCallback: zero interval is invalid") {
        PeriodicCallback pc(0.0, [] {});
        CHECK_FALSE(pc.isValid());
        CHECK_FALSE(pc.service());
}

TEST_CASE("PeriodicCallback: negative interval is invalid") {
        PeriodicCallback pc(-1.0, [] {});
        CHECK_FALSE(pc.isValid());
        CHECK_FALSE(pc.service());
}

TEST_CASE("PeriodicCallback: null function is invalid") {
        PeriodicCallback pc(1.0, nullptr);
        CHECK_FALSE(pc.isValid());
        CHECK_FALSE(pc.service());
}
