/**
 * @file      waitcondition.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <atomic>
#include <doctest/doctest.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>

using namespace promeki;

TEST_CASE("WaitCondition_WakeOne") {
        Mutex m;
        WaitCondition cv;
        bool ready = false;

        std::thread t([&] {
                m.lock();
                cv.wait(m, [&] { return ready; });
                m.unlock();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        {
                Mutex::Locker locker(m);
                ready = true;
        }
        cv.wakeOne();
        t.join();
        CHECK(ready);
}

TEST_CASE("WaitCondition_WakeOneNoPredicate") {
        Mutex m;
        WaitCondition cv;
        std::atomic<bool> woken{false};

        std::thread t([&] {
                m.lock();
                cv.wait(m);
                woken.store(true);
                m.unlock();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        cv.wakeOne();
        t.join();
        CHECK(woken.load());
}

TEST_CASE("WaitCondition_WakeAll") {
        Mutex m;
        WaitCondition cv;
        bool ready = false;
        std::atomic<int> wokenCount{0};

        auto waiter = [&] {
                m.lock();
                cv.wait(m, [&] { return ready; });
                wokenCount++;
                m.unlock();
        };

        std::thread t1(waiter);
        std::thread t2(waiter);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        {
                Mutex::Locker locker(m);
                ready = true;
        }
        cv.wakeAll();
        t1.join();
        t2.join();
        CHECK(wokenCount == 2);
}

TEST_CASE("WaitCondition_Timeout") {
        Mutex m;
        WaitCondition cv;
        m.lock();
        Error err = cv.wait(m, 10);
        m.unlock();
        // Spurious wakeup may cause Ok, but most likely Timeout
        // We just check it returns without hanging
        CHECK((err == Error::Ok || err == Error::Timeout));
}

TEST_CASE("WaitCondition_TimeoutWithPredicate") {
        Mutex m;
        WaitCondition cv;
        m.lock();
        Error err = cv.wait(m, [] { return false; }, 10);
        m.unlock();
        CHECK(err == Error::Timeout);
}

TEST_CASE("WaitCondition_PredicateAlreadyTrue") {
        Mutex m;
        WaitCondition cv;
        m.lock();
        Error err = cv.wait(m, [] { return true; }, 10);
        m.unlock();
        CHECK(err == Error::Ok);
}

TEST_CASE("WaitCondition_IndefinitePredicateWait") {
        Mutex m;
        WaitCondition cv;
        bool ready = false;

        std::thread t([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                {
                        Mutex::Locker locker(m);
                        ready = true;
                }
                cv.wakeOne();
        });

        m.lock();
        cv.wait(m, [&] { return ready; });
        CHECK(ready);
        m.unlock();
        t.join();
}
