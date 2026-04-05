/**
 * @file      readwritelock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/readwritelock.h>
#include <promeki/atomic.h>

using namespace promeki;

TEST_CASE("ReadWriteLock_ReadLock") {
        ReadWriteLock rwl;
        rwl.lockForRead();
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_WriteLock") {
        ReadWriteLock rwl;
        rwl.lockForWrite();
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_ConcurrentReads") {
        ReadWriteLock rwl;
        Atomic<int> readersInside(0);
        Atomic<int> maxConcurrentReaders(0);
        Atomic<int> ready(0);

        auto reader = [&] {
                // Ensure both threads are alive before acquiring the lock
                ready.fetchAndAdd(1);
                while(ready.value() < 2) std::this_thread::yield();

                ReadWriteLock::ReadLocker locker(rwl);
                int val = readersInside.fetchAndAdd(1) + 1;
                int prev = maxConcurrentReaders.value();
                while(val > prev && !maxConcurrentReaders.compareAndSwap(prev, val));
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                readersInside.fetchAndSub(1);
        };

        std::thread t1(reader);
        std::thread t2(reader);
        t1.join();
        t2.join();
        CHECK(maxConcurrentReaders.value() >= 2);
}

TEST_CASE("ReadWriteLock_WriteExcludesReaders") {
        ReadWriteLock rwl;
        rwl.lockForWrite();

        bool gotReadLock = false;
        std::thread t([&] {
                gotReadLock = rwl.tryLockForRead();
                if(gotReadLock) rwl.unlock();
        });
        t.join();
        CHECK_FALSE(gotReadLock);
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_WriteExcludesWriters") {
        ReadWriteLock rwl;
        rwl.lockForWrite();

        bool gotWriteLock = false;
        std::thread t([&] {
                gotWriteLock = rwl.tryLockForWrite();
                if(gotWriteLock) rwl.unlock();
        });
        t.join();
        CHECK_FALSE(gotWriteLock);
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_ReadLocker") {
        ReadWriteLock rwl;
        {
                ReadWriteLock::ReadLocker locker(rwl);
                CHECK(rwl.tryLockForRead());
                rwl.unlock();
        }
        CHECK(rwl.tryLockForWrite());
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_WriteLocker") {
        ReadWriteLock rwl;
        {
                ReadWriteLock::WriteLocker locker(rwl);
                bool got = false;
                std::thread t([&] {
                        got = rwl.tryLockForRead();
                        if(got) rwl.unlock();
                });
                t.join();
                CHECK_FALSE(got);
        }
        CHECK(rwl.tryLockForWrite());
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_ReadExcludesWriters") {
        ReadWriteLock rwl;
        rwl.lockForRead();

        bool gotWriteLock = false;
        std::thread t([&] {
                gotWriteLock = rwl.tryLockForWrite();
                if(gotWriteLock) rwl.unlock();
        });
        t.join();
        CHECK_FALSE(gotWriteLock);
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_TryLockForRead") {
        ReadWriteLock rwl;
        CHECK(rwl.tryLockForRead());
        rwl.unlock();
}

TEST_CASE("ReadWriteLock_TryLockForWrite") {
        ReadWriteLock rwl;
        CHECK(rwl.tryLockForWrite());
        rwl.unlock();
}
