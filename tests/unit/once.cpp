/**
 * @file      once.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <stdexcept>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/once.h>

using namespace promeki;

TEST_CASE("OnceFlag_RunsCallableExactlyOnce_SingleThread") {
        OnceFlag    flag;
        Atomic<int> count(0);
        for (int i = 0; i < 5; ++i) {
                callOnce(flag, [&] { count.fetchAndAdd(1, MemoryOrder::Relaxed); });
        }
        CHECK(count.value() == 1);
}

TEST_CASE("OnceFlag_RunsCallableExactlyOnce_Concurrent") {
        OnceFlag    flag;
        Atomic<int> count(0);

        // 32 threads race on the same flag.  Exactly one must run
        // the callable; all others must observe the side effect
        // before continuing.
        std::vector<std::thread> threads;
        threads.reserve(32);
        for (int i = 0; i < 32; ++i) {
                threads.emplace_back([&] {
                        callOnce(flag, [&] { count.fetchAndAdd(1, MemoryOrder::Relaxed); });
                        // After callOnce returns, the side effect
                        // must be visible to every thread.
                        CHECK(count.value() == 1);
                });
        }
        for (auto &t : threads) t.join();
        CHECK(count.value() == 1);
}

TEST_CASE("OnceFlag_ForwardsArguments") {
        OnceFlag flag;
        int      seen = 0;
        callOnce(
                flag,
                [&](int a, int b) { seen = a * 10 + b; },
                3, 4);
        CHECK(seen == 34);
}

TEST_CASE("OnceFlag_ExceptionLeavesFlagUnset") {
        // Documented contract: if the winning callable throws, the
        // flag stays unset and a subsequent callOnce tries again.
        OnceFlag flag;
        int      attempts = 0;
        CHECK_THROWS_AS(callOnce(flag,
                                 [&] {
                                         ++attempts;
                                         throw std::runtime_error("boom");
                                 }),
                        std::runtime_error);
        CHECK(attempts == 1);

        // Second attempt now succeeds — the side effect runs.
        callOnce(flag, [&] { ++attempts; });
        CHECK(attempts == 2);

        // Third call: flag is now set, no further calls.
        callOnce(flag, [&] { ++attempts; });
        CHECK(attempts == 2);
}

TEST_CASE("OnceFlag_NotCopyableNotMovable") {
        CHECK_FALSE(std::is_copy_constructible_v<OnceFlag>);
        CHECK_FALSE(std::is_copy_assignable_v<OnceFlag>);
        CHECK_FALSE(std::is_move_constructible_v<OnceFlag>);
        CHECK_FALSE(std::is_move_assignable_v<OnceFlag>);
}
