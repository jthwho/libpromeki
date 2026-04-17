/**
 * @file      elapsedtimer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <thread>
#include <chrono>
#include <promeki/elapsedtimer.h>

using namespace promeki;

TEST_CASE("ElapsedTimer: starts valid on construction") {
        ElapsedTimer t;
        CHECK(t.isValid());
}

TEST_CASE("ElapsedTimer: elapsed returns non-negative") {
        ElapsedTimer t;
        CHECK(t.elapsed() >= 0);
}

TEST_CASE("ElapsedTimer: elapsed increases over time") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(t.elapsed() >= 15);
}

TEST_CASE("ElapsedTimer: elapsedUs returns microseconds") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(t.elapsedUs() >= 5000);
}

TEST_CASE("ElapsedTimer: elapsedNs returns nanoseconds") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(t.elapsedNs() >= 5000000);
}

TEST_CASE("ElapsedTimer: restart returns elapsed and resets") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        int64_t prev = t.restart();
        CHECK(prev >= 15);
        CHECK(t.elapsed() < prev);
}

TEST_CASE("ElapsedTimer: hasExpired") {
        ElapsedTimer t;
        CHECK_FALSE(t.hasExpired(10000));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(t.hasExpired(15));
}

TEST_CASE("ElapsedTimer: invalidate and isValid") {
        ElapsedTimer t;
        CHECK(t.isValid());
        t.invalidate();
        CHECK_FALSE(t.isValid());
}

TEST_CASE("ElapsedTimer: start revalidates after invalidate") {
        ElapsedTimer t;
        t.invalidate();
        CHECK_FALSE(t.isValid());
        t.start();
        CHECK(t.isValid());
}

TEST_CASE("ElapsedTimer: start resets elapsed time") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(t.elapsed() >= 15);
        t.start();
        CHECK(t.elapsed() < 10);
}

TEST_CASE("ElapsedTimer: restart revalidates after invalidate") {
        ElapsedTimer t;
        t.invalidate();
        CHECK_FALSE(t.isValid());
        int64_t prev = t.restart();
        CHECK(t.isValid());
        // prev is meaningless since we were invalid, but it shouldn't crash
        (void)prev;
}

TEST_CASE("ElapsedTimer: multiple restarts accumulate correctly") {
        ElapsedTimer t;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        int64_t first = t.restart();
        CHECK(first >= 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        int64_t second = t.restart();
        CHECK(second >= 10);
}
