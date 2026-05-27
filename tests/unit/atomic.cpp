/**
 * @file      atomic.cpp
 * @copyright Jason Howard. All rights reserved.
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

// ============================================================================
// MemoryOrder translation table.
// ============================================================================

TEST_CASE("MemoryOrder_TranslatesToStdMemoryOrder") {
        CHECK(toStdMemoryOrder(MemoryOrder::Relaxed) == std::memory_order_relaxed);
        CHECK(toStdMemoryOrder(MemoryOrder::Consume) == std::memory_order_consume);
        CHECK(toStdMemoryOrder(MemoryOrder::Acquire) == std::memory_order_acquire);
        CHECK(toStdMemoryOrder(MemoryOrder::Release) == std::memory_order_release);
        CHECK(toStdMemoryOrder(MemoryOrder::AcqRel)  == std::memory_order_acq_rel);
        CHECK(toStdMemoryOrder(MemoryOrder::SeqCst)  == std::memory_order_seq_cst);
}

TEST_CASE("Atomic_ExplicitOrderLoadStore") {
        Atomic<int> a(0);
        a.store(11, MemoryOrder::Release);
        CHECK(a.load(MemoryOrder::Acquire) == 11);
        // Default-order load = SeqCst — must observe latest store too.
        CHECK(a.load() == 11);
}

TEST_CASE("Atomic_ExplicitOrderFetchOps") {
        Atomic<int> a(0);
        // fetchAndAdd default order (AcqRel).
        CHECK(a.fetchAndAdd(5) == 0);
        CHECK(a.value() == 5);
        // Explicit relaxed.
        CHECK(a.fetchAndAdd(3, MemoryOrder::Relaxed) == 5);
        CHECK(a.value() == 8);
        CHECK(a.fetchAndSub(2, MemoryOrder::Relaxed) == 8);
        CHECK(a.value() == 6);
}

TEST_CASE("Atomic_FetchAndBitwise") {
        Atomic<uint32_t> a(0xFu);
        CHECK(a.fetchAndAnd(0x6u) == 0xFu);
        CHECK(a.value() == 0x6u);
        CHECK(a.fetchAndOr(0x1u) == 0x6u);
        CHECK(a.value() == 0x7u);
        CHECK(a.fetchAndXor(0x4u) == 0x7u);
        CHECK(a.value() == 0x3u);
}

TEST_CASE("Atomic_CompareExchangeStrong_DefaultOrder") {
        Atomic<int> a(7);
        int         expected = 7;
        CHECK(a.compareExchangeStrong(expected, 8));
        CHECK(a.value() == 8);
        expected = 7; // wrong
        CHECK_FALSE(a.compareExchangeStrong(expected, 9));
        CHECK(expected == 8); // updated to current
        CHECK(a.value() == 8);
}

TEST_CASE("Atomic_CompareExchangeStrong_ExplicitOrders") {
        Atomic<int> a(0);
        int         expected = 0;
        CHECK(a.compareExchangeStrong(expected, 42, MemoryOrder::AcqRel, MemoryOrder::Acquire));
        CHECK(a.value() == 42);
}

TEST_CASE("Atomic_CompareExchangeWeak_RetryLoop") {
        // Weak CAS may fail spuriously; the documented contract is
        // "retry in a loop".  This loop must terminate and update.
        Atomic<int> a(0);
        int         expected = 0;
        while (!a.compareExchangeWeak(expected, expected + 1, MemoryOrder::AcqRel,
                                      MemoryOrder::Acquire)) {
                // expected updated by failure path; loop continues.
        }
        CHECK(a.value() == 1);
}

TEST_CASE("Atomic_OperatorIncrementDecrement") {
        Atomic<int> a(10);
        CHECK(++a == 11);   // pre-increment returns new value
        CHECK(a.value() == 11);
        CHECK(a++ == 11);   // post-increment returns old
        CHECK(a.value() == 12);
        CHECK(--a == 11);
        CHECK(a.value() == 11);
        CHECK(a-- == 11);
        CHECK(a.value() == 10);
}

TEST_CASE("Atomic_ConstructorIsExplicit") {
        // Atomic's constructor must be explicit so a bare `5` won't
        // implicitly convert into an Atomic<int> in argument
        // passing or copy-init.  Confirmed via std::is_convertible.
        CHECK_FALSE(std::is_convertible_v<int, Atomic<int>>);
        CHECK(std::is_constructible_v<Atomic<int>, int>);
}

// ============================================================================
// AtomicRef — reference-style atomic over externally-owned storage.
// ============================================================================

TEST_CASE("AtomicRef_BasicLoadStore") {
        alignas(std::atomic_ref<int>::required_alignment) int storage = 0;
        AtomicRef<int>                                       ref(storage);
        ref.store(123, MemoryOrder::Release);
        CHECK(ref.load(MemoryOrder::Acquire) == 123);
        CHECK(storage == 123); // raw storage updated
}

TEST_CASE("AtomicRef_FetchOps") {
        alignas(std::atomic_ref<int>::required_alignment) int storage = 10;
        AtomicRef<int>                                       ref(storage);
        CHECK(ref.fetchAndAdd(5) == 10);
        CHECK(ref.load(MemoryOrder::Relaxed) == 15);
        CHECK(ref.fetchAndSub(3) == 15);
        CHECK(ref.load(MemoryOrder::Relaxed) == 12);
}

TEST_CASE("AtomicRef_Exchange") {
        alignas(std::atomic_ref<int>::required_alignment) int storage = 1;
        AtomicRef<int>                                       ref(storage);
        CHECK(ref.exchange(99) == 1);
        CHECK(storage == 99);
}

TEST_CASE("AtomicRef_CompareExchange") {
        alignas(std::atomic_ref<int>::required_alignment) int storage = 5;
        AtomicRef<int>                                       ref(storage);
        int                                                  expected = 5;
        CHECK(ref.compareExchangeStrong(expected, 7, MemoryOrder::AcqRel, MemoryOrder::Acquire));
        CHECK(storage == 7);
        expected = 5; // wrong now
        CHECK_FALSE(ref.compareExchangeStrong(expected, 8, MemoryOrder::AcqRel,
                                              MemoryOrder::Acquire));
        CHECK(expected == 7);
        CHECK(storage == 7);
}

TEST_CASE("AtomicRef_MultipleRefsToSameStorage") {
        // C++20 explicitly allows multiple AtomicRef instances to the
        // same storage at once, as long as the storage is never
        // accessed through plain (non-atomic) reads/writes.
        alignas(std::atomic_ref<uint64_t>::required_alignment) uint64_t storage = 0;
        AtomicRef<uint64_t>                                            a(storage);
        AtomicRef<uint64_t>                                            b(storage);
        a.store(1, MemoryOrder::Release);
        CHECK(b.load(MemoryOrder::Acquire) == 1);
        b.fetchAndAdd(10, MemoryOrder::AcqRel);
        CHECK(a.load(MemoryOrder::Acquire) == 11);
}

// ============================================================================
// AtomicFlag — guaranteed lock-free boolean.
// ============================================================================

TEST_CASE("AtomicFlag_StartsCleared") {
        AtomicFlag f;
        CHECK_FALSE(f.test());
}

TEST_CASE("AtomicFlag_TestAndSet") {
        AtomicFlag f;
        // First testAndSet: caller "wins" the race (returns false = was unset).
        CHECK_FALSE(f.testAndSet());
        CHECK(f.test());
        // Second testAndSet: caller "loses" (returns true = was already set).
        CHECK(f.testAndSet());
}

TEST_CASE("AtomicFlag_Clear") {
        AtomicFlag f;
        f.testAndSet();
        CHECK(f.test());
        f.clear();
        CHECK_FALSE(f.test());
}

TEST_CASE("AtomicFlag_SpinLockPattern") {
        // One-shot init guard: exactly one thread runs the critical
        // section; subsequent callers see the flag already set and
        // skip.
        AtomicFlag inited;
        Atomic<int> initCount(0);
        auto worker = [&] {
                if (!inited.testAndSet(MemoryOrder::Acquire)) {
                        initCount.fetchAndAdd(1, MemoryOrder::Relaxed);
                }
        };
        std::thread t1(worker);
        std::thread t2(worker);
        std::thread t3(worker);
        t1.join();
        t2.join();
        t3.join();
        CHECK(initCount.value() == 1);
}

// ============================================================================
// atomicThreadFence — verify it links and the call compiles.  Memory
// ordering between threads is not directly observable in a unit test
// without TSAN; this exercise is mostly a "did it compile and not
// crash" check.
// ============================================================================

TEST_CASE("AtomicThreadFence_AllOrderings") {
        atomicThreadFence(MemoryOrder::Relaxed);
        atomicThreadFence(MemoryOrder::Acquire);
        atomicThreadFence(MemoryOrder::Release);
        atomicThreadFence(MemoryOrder::AcqRel);
        atomicThreadFence(MemoryOrder::SeqCst);
        CHECK(true);
}
