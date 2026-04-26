/**
 * @file      atomic.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <doctest/doctest.h>
#include <promeki/atomic.h>
#include <promeki/streamstring.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("Atomic_DefaultConstruction") {
        Atomic<int> a;
        CHECK(a.value() == 0);
}

TEST_CASE("Atomic_ValueConstruction") {
        Atomic<int> a(42);
        CHECK(a.value() == 42);
}

TEST_CASE("Atomic_SetValue") {
        Atomic<int> a(0);
        a.setValue(99);
        CHECK(a.value() == 99);
}

TEST_CASE("Atomic_FetchAndAdd") {
        Atomic<int> a(10);
        int         old = a.fetchAndAdd(5);
        CHECK(old == 10);
        CHECK(a.value() == 15);
}

TEST_CASE("Atomic_FetchAndSub") {
        Atomic<int> a(10);
        int         old = a.fetchAndSub(3);
        CHECK(old == 10);
        CHECK(a.value() == 7);
}

TEST_CASE("Atomic_CompareAndSwap_Success") {
        Atomic<int> a(42);
        int         expected = 42;
        bool        swapped = a.compareAndSwap(expected, 100);
        CHECK(swapped);
        CHECK(a.value() == 100);
}

TEST_CASE("Atomic_CompareAndSwap_Failure") {
        Atomic<int> a(42);
        int         expected = 99;
        bool        swapped = a.compareAndSwap(expected, 100);
        CHECK_FALSE(swapped);
        CHECK(expected == 42);
        CHECK(a.value() == 42);
}

TEST_CASE("Atomic_Exchange") {
        Atomic<int> a(42);
        int         old = a.exchange(100);
        CHECK(old == 42);
        CHECK(a.value() == 100);
}

TEST_CASE("Atomic_ConcurrentFetchAndAdd") {
        Atomic<int> a(0);
        const int   iterations = 10000;

        auto adder = [&] {
                for (int i = 0; i < iterations; i++) {
                        a.fetchAndAdd(1);
                }
        };

        std::thread t1(adder);
        std::thread t2(adder);
        t1.join();
        t2.join();
        CHECK(a.value() == iterations * 2);
}

TEST_CASE("Atomic_TextStreamOperator") {
        Atomic<int>  a(7);
        StreamString out;
        out.stream() << a << promeki::flush;
        CHECK(out.line() == "7");
}
