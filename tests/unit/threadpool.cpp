/**
 * @file      threadpool.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/threadpool.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("ThreadPool_SubmitAndGet") {
        ThreadPool pool(2);
        auto       f = pool.submit([] { return 42; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 42);
}

TEST_CASE("ThreadPool_MultipleTasks") {
        ThreadPool        pool(2);
        const int         count = 20;
        List<Future<int>> futures;
        for (int i = 0; i < count; i++) {
                futures.pushToBack(pool.submit([i] { return i * 2; }));
        }
        for (int i = 0; i < count; i++) {
                auto [val, err] = futures[i].result();
                CHECK(err == Error::Ok);
                CHECK(val == i * 2);
        }
}

TEST_CASE("ThreadPool_WaitForDone") {
        ThreadPool       pool(2);
        std::atomic<int> counter{0};
        for (int i = 0; i < 10; i++) {
                pool.submit([&] { counter++; });
        }
        pool.waitForDone();
        CHECK(counter == 10);
}

TEST_CASE("ThreadPool_WaitForDoneTimeout") {
        ThreadPool pool(1);
        pool.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
        Error err = pool.waitForDone(10);
        CHECK(err == Error::Timeout);
        pool.waitForDone();
}

TEST_CASE("ThreadPool_ThreadCount") {
        ThreadPool pool(4);
        CHECK(pool.maxThreadCount() == 4);
        CHECK(pool.threadCount() == 0); // lazy: no threads until work arrives
        auto f = pool.submit([] { return 1; });
        f.waitForFinished();
        CHECK(pool.threadCount() == 1); // one thread spawned on demand
}

TEST_CASE("ThreadPool_ThreadCount_Eager") {
        ThreadPool pool(4, false);
        CHECK(pool.maxThreadCount() == 4);
        CHECK(pool.threadCount() == 4); // all threads pre-spawned
}

TEST_CASE("ThreadPool_InlineMode") {
        ThreadPool pool(0);
        CHECK(pool.threadCount() == 0);
        std::thread::id callerThread = std::this_thread::get_id();
        std::thread::id taskThread;
        auto            f = pool.submit([&] {
                taskThread = std::this_thread::get_id();
                return 99;
        });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 99);
        CHECK(callerThread == taskThread);
}

TEST_CASE("ThreadPool_SetThreadCount") {
        ThreadPool pool(2);
        CHECK(pool.maxThreadCount() == 2);
        pool.setThreadCount(4);
        CHECK(pool.maxThreadCount() == 4);

        // Verify pool still works after resize
        auto f = pool.submit([] { return 42; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 42);
}

TEST_CASE("ThreadPool_SetThreadCountToZero") {
        ThreadPool pool(2);
        pool.setThreadCount(0);
        CHECK(pool.threadCount() == 0);

        // Verify inline mode works
        auto f = pool.submit([] { return 7; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 7);
}

TEST_CASE("ThreadPool_Clear") {
        ThreadPool pool(1);
        // Submit a blocking task to occupy the thread
        pool.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
        // Submit more tasks that should be clearable
        std::atomic<int> counter{0};
        for (int i = 0; i < 100; i++) {
                pool.submit([&] { counter++; });
        }
        pool.clear();
        pool.waitForDone();
        // Some tasks may have started before clear, but not all 100
        CHECK(counter < 100);
}

TEST_CASE("ThreadPool_VoidTasks") {
        ThreadPool       pool(2);
        std::atomic<int> counter{0};
        auto             f = pool.submit([&] { counter++; });
        f.waitForFinished();
        CHECK(counter == 1);
}

TEST_CASE("ThreadPool_ActiveThreadCount") {
        ThreadPool pool(4);
        // Initially no active threads
        CHECK(pool.activeThreadCount() == 0);
}

TEST_CASE("ThreadPool_NamePrefix") {
        ThreadPool pool(2);
        pool.setNamePrefix("test");
        CHECK(pool.namePrefix() == String("test"));
        // Verify threads can run with the name prefix set
        auto f = pool.submit([] { return 1; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 1);
}

TEST_CASE("ThreadPool_LazySpawn") {
        ThreadPool pool(4);
        CHECK(pool.threadCount() == 0);

        // Submit tasks that block so we can observe thread growth
        std::atomic<int> barrier{0};
        std::atomic<int> running{0};
        auto             blockingTask = [&] {
                running++;
                while (barrier.load() == 0) {
                        std::this_thread::yield();
                }
        };

        // Submit 3 blocking tasks — should spawn 3 threads
        Future<void> f1 = pool.submit(blockingTask);
        Future<void> f2 = pool.submit(blockingTask);
        Future<void> f3 = pool.submit(blockingTask);
        // Wait for threads to actually start running
        while (running.load() < 3) std::this_thread::yield();
        CHECK(pool.threadCount() >= 3);
        CHECK(pool.threadCount() <= 4);

        // Release and clean up
        barrier.store(1);
        pool.waitForDone();
}

TEST_CASE("ThreadPool_DefaultConstructor") {
        ThreadPool pool;
        CHECK(pool.maxThreadCount() > 0);
        auto f = pool.submit([] { return 42; });
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 42);
}
