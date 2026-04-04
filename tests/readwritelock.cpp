/**
 * @file      readwritelock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/readwritelock.h>

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
        std::atomic<int> readersInside{0};
        int maxConcurrentReaders = 0;

        auto reader = [&] {
                ReadWriteLock::ReadLocker locker(rwl);
                int val = readersInside.fetch_add(1) + 1;
                if(val > maxConcurrentReaders) maxConcurrentReaders = val;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                readersInside.fetch_sub(1);
        };

        std::thread t1(reader);
        std::thread t2(reader);
        t1.join();
        t2.join();
        CHECK(maxConcurrentReaders >= 2);
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
