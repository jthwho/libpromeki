/**
 * @file      future.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <doctest/doctest.h>
#include <promeki/future.h>
#include <promeki/promise.h>
#include <promeki/thread.h>

using namespace promeki;

TEST_CASE("Future_DefaultConstruction") {
        Future<int> f;
        CHECK_FALSE(f.isValid());
        CHECK_FALSE(f.isReady());
}

TEST_CASE("Future_InvalidResult") {
        Future<int> f;
        auto [val, err] = f.result();
        CHECK(err == Error::Invalid);
}

TEST_CASE("Future_SetAndGet") {
        Promise<int> p;
        Future<int>  f = p.future();
        CHECK(f.isValid());
        CHECK_FALSE(f.isReady());

        p.setValue(42);
        CHECK(f.isReady());
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 42);
}

TEST_CASE("Future_Timeout") {
        Promise<int> p;
        Future<int>  f = p.future();
        auto [val, err] = f.result(10);
        CHECK(err == Error::Timeout);
}

TEST_CASE("Future_WaitForFinished") {
        Promise<int> p;
        Future<int>  f = p.future();

        std::thread t([&] {
                Thread::sleepMs(10);
                p.setValue(99);
        });

        f.waitForFinished();
        CHECK(f.isReady());
        t.join();
}

TEST_CASE("Future_WaitForFinishedTimeout") {
        Promise<int> p;
        Future<int>  f = p.future();
        Error        err = f.waitForFinished(10);
        CHECK(err == Error::Timeout);
        p.setValue(0);
}

TEST_CASE("Future_WaitForFinishedInvalid") {
        Future<int> f;
        Error       err = f.waitForFinished(10);
        CHECK(err == Error::Invalid);
}

TEST_CASE("Future_MoveOnly") {
        Promise<int> p;
        Future<int>  f1 = p.future();
        Future<int>  f2 = std::move(f1);
        CHECK_FALSE(f1.isValid());
        CHECK(f2.isValid());
        p.setValue(42);
        auto [val, err] = f2.result();
        CHECK(val == 42);
}

TEST_CASE("Future_Void") {
        Promise<void> p;
        Future<void>  f = p.future();
        CHECK(f.isValid());
        CHECK_FALSE(f.isReady());

        p.setValue();
        CHECK(f.isReady());
        Error err = f.result();
        CHECK(err == Error::Ok);
}

TEST_CASE("Future_VoidTimeout") {
        Promise<void> p;
        Future<void>  f = p.future();
        Error         err = f.result(10);
        CHECK(err == Error::Timeout);
        p.setValue();
}

TEST_CASE("Future_VoidInvalidResult") {
        Future<void> f;
        Error        err = f.result();
        CHECK(err == Error::Invalid);
}

TEST_CASE("Future_VoidWaitForFinished") {
        Promise<void> p;
        Future<void>  f = p.future();

        std::thread t([&] {
                Thread::sleepMs(10);
                p.setValue();
        });

        f.waitForFinished();
        CHECK(f.isReady());
        t.join();
}

TEST_CASE("Future_VoidWaitForFinishedTimeout") {
        Promise<void> p;
        Future<void>  f = p.future();
        Error         err = f.waitForFinished(10);
        CHECK(err == Error::Timeout);
        p.setValue();
}

TEST_CASE("Future_VoidMoveOnly") {
        Promise<void> p;
        Future<void>  f1 = p.future();
        Future<void>  f2 = std::move(f1);
        CHECK_FALSE(f1.isValid());
        CHECK(f2.isValid());
        p.setValue();
        Error err = f2.result();
        CHECK(err == Error::Ok);
}

TEST_CASE("Future_ThreadedSetGet") {
        Promise<int> p;
        Future<int>  f = p.future();

        std::thread t([&] {
                Thread::sleepMs(20);
                p.setValue(123);
        });

        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 123);
        t.join();
}

TEST_CASE("Promise_MoveSemantics") {
        Promise<int> p1;
        Future<int>  f = p1.future();
        Promise<int> p2 = std::move(p1);
        p2.setValue(77);
        auto [val, err] = f.result();
        CHECK(err == Error::Ok);
        CHECK(val == 77);
}

TEST_CASE("Promise_VoidMoveSemantics") {
        Promise<void> p1;
        Future<void>  f = p1.future();
        Promise<void> p2 = std::move(p1);
        p2.setValue();
        Error err = f.result();
        CHECK(err == Error::Ok);
}
