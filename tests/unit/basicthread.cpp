/**
 * @file      basicthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <doctest/doctest.h>

#include <promeki/basicthread.h>
#include <promeki/duration.h>
#include <promeki/elapsedtimer.h>
#include <promeki/error.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <pthread.h>
#endif

using namespace promeki;

TEST_CASE("BasicThread: default-constructed is empty") {
        BasicThread bt;
        CHECK_FALSE(bt.isJoinable());
        CHECK_FALSE(bt.isRunning());
        CHECK(bt.name().isEmpty());
        CHECK(bt.nativeId() == 0);
        CHECK(bt.id() == std::thread::id{});
}

TEST_CASE("BasicThread: start, join, captured ids are non-default") {
        BasicThread       bt("test-basic");
        std::atomic<bool> ran{false};
        std::atomic<bool> sawCurrent{false};
        Error             err = bt.start([&]() {
                ran.store(true);
                sawCurrent.store(bt.isCurrentThread());
        });
        REQUIRE(err == Error::Ok);
        CHECK(bt.join() == Error::Ok);
        CHECK(ran.load());
        CHECK(sawCurrent.load());
        CHECK(bt.nativeId() != 0);
        CHECK(bt.id() != std::thread::id{});
        CHECK_FALSE(bt.isJoinable());
}

TEST_CASE("BasicThread: name persists and is queryable") {
        BasicThread bt("hello");
        CHECK(bt.name() == "hello");
        bt.setName("world");
        CHECK(bt.name() == "world");
}

#if defined(PROMEKI_PLATFORM_LINUX)
TEST_CASE("BasicThread: OS-level name is applied on start (Linux)") {
        BasicThread       bt("named-bt");
        std::atomic<bool> ok{false};
        Error             err = bt.start([&]() {
                char buf[32] = {0};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                ok.store(std::string(buf) == "named-bt");
        });
        REQUIRE(err == Error::Ok);
        bt.join();
        CHECK(ok.load());
}
#endif

TEST_CASE("BasicThread: empty entry returns Error::Invalid") {
        BasicThread        bt;
        BasicThread::Entry empty;
        Error              err = bt.start(empty);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(bt.isJoinable());
}

TEST_CASE("BasicThread: cannot start twice without join") {
        BasicThread       bt("twice");
        std::atomic<bool> hold{true};
        REQUIRE(bt.start([&]() {
                while (hold.load()) BasicThread::sleepMs(1);
        }) == Error::Ok);
        Error second = bt.start([]() {});
        CHECK(second == Error::Busy);
        hold.store(false);
        bt.join();
}

TEST_CASE("BasicThread: move construction transfers ownership") {
        BasicThread       a("mover");
        std::atomic<bool> ran{false};
        REQUIRE(a.start([&]() {
                BasicThread::sleepMs(5);
                ran.store(true);
        }) == Error::Ok);
        BasicThread b(std::move(a));
        // After move, source is empty
        CHECK_FALSE(a.isJoinable());
        CHECK(b.isJoinable());
        b.join();
        CHECK(ran.load());
}

TEST_CASE("BasicThread: destructor joins automatically") {
        std::atomic<bool> ran{false};
        {
                BasicThread bt("auto-join");
                REQUIRE(bt.start([&]() {
                        BasicThread::sleepMs(5);
                        ran.store(true);
                }) == Error::Ok);
        }
        CHECK(ran.load());
}

TEST_CASE("BasicThread: stack size path (POSIX) starts and joins") {
        BasicThread       bt("stackd");
        std::atomic<bool> ran{false};
        Error             err = bt.start([&]() { ran.store(true); }, 256 * 1024);
        REQUIRE(err == Error::Ok);
        bt.join();
        CHECK(ran.load());
}

TEST_CASE("BasicThread::sleepMs sleeps at least the requested duration") {
        ElapsedTimer timer;
        BasicThread::sleepMs(20);
        CHECK(timer.elapsed() >= 19);
}

TEST_CASE("BasicThread::sleepUs sleeps at least the requested duration") {
        ElapsedTimer timer;
        BasicThread::sleepUs(5'000);
        CHECK(timer.elapsedUs() >= 4'500);
}

TEST_CASE("BasicThread::sleepNs sleeps at least the requested duration") {
        ElapsedTimer timer;
        BasicThread::sleepNs(2'000'000);
        CHECK(timer.elapsedNs() >= 1'500'000);
}

TEST_CASE("BasicThread::sleep(Duration) sleeps at least the requested duration") {
        ElapsedTimer timer;
        BasicThread::sleep(Duration::fromMilliseconds(15));
        CHECK(timer.elapsed() >= 14);
}

TEST_CASE("BasicThread::sleep with non-positive arguments returns immediately") {
        ElapsedTimer timer;
        BasicThread::sleepMs(0);
        BasicThread::sleepUs(-100);
        BasicThread::sleepNs(-1);
        BasicThread::sleep(Duration::fromMilliseconds(-5));
        BasicThread::sleep(Duration());
        CHECK(timer.elapsed() < 5);
}

TEST_CASE("BasicThread::idealThreadCount returns a sensible value") {
        unsigned int n = BasicThread::idealThreadCount();
        CHECK(n >= 1u);
}

TEST_CASE("BasicThread::currentNativeId returns non-zero and is stable") {
        uint64_t id = BasicThread::currentNativeId();
        CHECK(id != 0);
        CHECK(BasicThread::currentNativeId() == id);
}

TEST_CASE("BasicThread::priorityMin/Max bound the priority range") {
        int lo = BasicThread::priorityMin(SchedulePolicy::Default);
        int hi = BasicThread::priorityMax(SchedulePolicy::Default);
        CHECK(lo <= hi);
}

TEST_CASE("BasicThread: setName before start applies OS name when started") {
#if defined(PROMEKI_PLATFORM_LINUX)
        BasicThread bt;
        bt.setName("preset");
        std::atomic<bool> match{false};
        Error             err = bt.start([&]() {
                char buf[32] = {0};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                match.store(std::string(buf) == "preset");
        });
        REQUIRE(err == Error::Ok);
        bt.join();
        CHECK(match.load());
#endif
}

TEST_CASE("BasicThread: nativeId observed from inside the thread matches accessor") {
        BasicThread           bt("idcheck");
        std::atomic<uint64_t> insideId{0};
        Error                 err = bt.start([&]() { insideId.store(BasicThread::currentNativeId()); });
        REQUIRE(err == Error::Ok);
        bt.join();
        CHECK(insideId.load() != 0);
        CHECK(bt.nativeId() == insideId.load());
}
