/**
 * @file      strand.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/strand.h>
#include <promeki/threadpool.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {
        // Convenience for the short sleeps used by these tests.
        inline void sleepUs(int us) {
                TimeStamp::sleep(std::chrono::microseconds(us));
        }
}

TEST_CASE("Strand_SubmitAndGet") {
        ThreadPool pool(4);
        Strand strand(pool);
        auto f = strand.submit([] { return 42; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 42);
}

TEST_CASE("Strand_SerialOrder") {
        // Tasks must execute in submission order, not concurrently.
        ThreadPool pool(4);
        Strand strand(pool);
        List<int> order;
        Mutex orderMutex;
        const int count = 100;

        for(int i = 0; i < count; i++) {
                strand.submit([i, &order, &orderMutex] {
                        // Force overlap if serialization is broken
                        sleepUs(50);
                        Mutex::Locker lock(orderMutex);
                        order.pushToBack(i);
                });
        }
        strand.waitForIdle();

        REQUIRE((int)order.size() == count);
        for(int i = 0; i < count; i++) {
                CHECK(order[i] == i);
        }
}

TEST_CASE("Strand_NoOverlap") {
        // No two strand tasks should run concurrently.
        ThreadPool pool(4);
        Strand strand(pool);
        Atomic<int> active{0};
        Atomic<int> maxActive{0};
        const int count = 50;

        for(int i = 0; i < count; i++) {
                strand.submit([&active, &maxActive] {
                        int now = active.fetchAndAdd(1) + 1;
                        int prev = maxActive.value();
                        while(now > prev && !maxActive.compareAndSwap(prev, now)) {}
                        sleepUs(100);
                        active.fetchAndSub(1);
                });
        }
        strand.waitForIdle();

        CHECK(maxActive.value() == 1);
}

TEST_CASE("Strand_VoidTask") {
        ThreadPool pool(2);
        Strand strand(pool);
        Atomic<int> counter{0};
        for(int i = 0; i < 10; i++) {
                strand.submit([&counter] { counter.fetchAndAdd(1); });
        }
        strand.waitForIdle();
        CHECK(counter.value() == 10);
}

TEST_CASE("Strand_MultipleStrandsConcurrent") {
        // Independent strands should be able to run concurrently.
        ThreadPool pool(4);
        Strand strandA(pool);
        Strand strandB(pool);

        Atomic<int> counterA{0};
        Atomic<int> counterB{0};

        for(int i = 0; i < 50; i++) {
                strandA.submit([&counterA] { counterA.fetchAndAdd(1); });
                strandB.submit([&counterB] { counterB.fetchAndAdd(1); });
        }
        strandA.waitForIdle();
        strandB.waitForIdle();

        CHECK(counterA.value() == 50);
        CHECK(counterB.value() == 50);
}

TEST_CASE("Strand_IsBusy") {
        ThreadPool pool(2);
        Strand strand(pool);
        CHECK_FALSE(strand.isBusy());
        strand.submit([] {
                TimeStamp::sleep(std::chrono::milliseconds(50));
        });
        // Busy may be true immediately after submit; either is acceptable
        // depending on scheduling.  Just make sure waitForIdle clears it.
        strand.waitForIdle();
        CHECK_FALSE(strand.isBusy());
}

TEST_CASE("Strand_DestructorWaitsForIdle") {
        ThreadPool pool(2);
        Atomic<int> counter{0};
        {
                Strand strand(pool);
                for(int i = 0; i < 20; i++) {
                        strand.submit([&counter] {
                                sleepUs(100);
                                counter.fetchAndAdd(1);
                        });
                }
                // Destructor must wait for all tasks
        }
        CHECK(counter.value() == 20);
}

TEST_CASE("Strand_CancelPending") {
        // Cancelling drains queued tasks; their futures resolve with Cancelled.
        ThreadPool pool(2);
        Strand strand(pool);

        // Submit a task that holds the strand busy long enough that
        // subsequent submissions queue up behind it.
        Atomic<bool> firstStarted{false};
        Atomic<bool> firstUnblock{false};
        auto firstFuture = strand.submit([&]() -> Error {
                firstStarted.setValue(true);
                while(!firstUnblock.value()) {
                        sleepUs(50);
                }
                return Error::Ok;
        });

        // Wait for the first task to actually start so the next ones queue.
        while(!firstStarted.value()) {
                sleepUs(50);
        }

        Atomic<int> ranCount{0};
        Future<Error> queued1 = strand.submit([&]() -> Error { ranCount.fetchAndAdd(1); return Error::Ok; });
        Future<Error> queued2 = strand.submit([&]() -> Error { ranCount.fetchAndAdd(1); return Error::Ok; });
        Future<Error> queued3 = strand.submit([&]() -> Error { ranCount.fetchAndAdd(1); return Error::Ok; });

        size_t cancelled = strand.cancelPending();
        CHECK(cancelled == 3);

        // Cancelled futures resolve with Cancelled error.
        CHECK(queued1.result().second() == Error::Cancelled);
        CHECK(queued2.result().second() == Error::Cancelled);
        CHECK(queued3.result().second() == Error::Cancelled);

        // Let the first task finish — it should not have been cancelled.
        firstUnblock.setValue(true);
        CHECK(firstFuture.result().first() == Error::Ok);

        // None of the cancelled tasks ran.
        CHECK(ranCount.value() == 0);
}

TEST_CASE("Strand_CancelEmptyQueue") {
        ThreadPool pool(2);
        Strand strand(pool);
        CHECK(strand.cancelPending() == 0);
}

TEST_CASE("Strand_CancelHookRuns") {
        // The optional cancel hook fires when a task is cancelled.
        ThreadPool pool(2);
        Strand strand(pool);

        // Block the strand with a long task.
        Atomic<bool> unblock{false};
        Atomic<bool> firstStarted{false};
        strand.submit([&] {
                firstStarted.setValue(true);
                while(!unblock.value()) {
                        sleepUs(50);
                }
        });
        while(!firstStarted.value()) {
                sleepUs(50);
        }

        Atomic<int> cancelHookCount{0};
        Atomic<int> ranCount{0};
        for(int i = 0; i < 5; i++) {
                strand.submit(
                        [&] { ranCount.fetchAndAdd(1); },
                        [&] { cancelHookCount.fetchAndAdd(1); });
        }

        size_t cancelled = strand.cancelPending();
        CHECK(cancelled == 5);
        CHECK(cancelHookCount.value() == 5);

        unblock.setValue(true);
        strand.waitForIdle();

        // The 5 cancelled tasks didn't run; only the first long task did.
        CHECK(ranCount.value() == 0);
}
