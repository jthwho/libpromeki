/**
 * @file      mutex.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <doctest/doctest.h>
#include <promeki/mutex.h>

using namespace promeki;

TEST_CASE("Mutex_LockUnlock") {
        Mutex m;
        m.lock();
        m.unlock();
}

TEST_CASE("Mutex_TryLock") {
        Mutex m;
        CHECK(m.tryLock());
        // Mutex is now locked; tryLock from same thread is UB for std::mutex,
        // so we unlock and verify tryLock succeeds again.
        m.unlock();
        CHECK(m.tryLock());
        m.unlock();
}

TEST_CASE("Mutex_TryLockContended") {
        Mutex m;
        m.lock();
        bool        gotLock = false;
        std::thread t([&] {
                gotLock = m.tryLock();
                if (gotLock) m.unlock();
        });
        t.join();
        CHECK_FALSE(gotLock);
        m.unlock();
}

TEST_CASE("Mutex_Locker") {
        Mutex m;
        {
                Mutex::Locker locker(m);
                // Mutex should be locked; another thread can't acquire it
                bool        gotLock = false;
                std::thread t([&] {
                        gotLock = m.tryLock();
                        if (gotLock) m.unlock();
                });
                t.join();
                CHECK_FALSE(gotLock);
        }
        // After Locker goes out of scope, mutex should be unlocked
        CHECK(m.tryLock());
        m.unlock();
}

TEST_CASE("Mutex_MutualExclusion") {
        Mutex     m;
        int       counter = 0;
        const int iterations = 10000;

        auto increment = [&] {
                for (int i = 0; i < iterations; i++) {
                        Mutex::Locker locker(m);
                        counter++;
                }
        };

        std::thread t1(increment);
        std::thread t2(increment);
        t1.join();
        t2.join();
        CHECK(counter == iterations * 2);
}
