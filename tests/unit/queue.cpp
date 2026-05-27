/**
 * @file      queue.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <atomic>
#include <string>
#include <doctest/doctest.h>
#include <promeki/queue.h>
#include <promeki/error.h>
#include <promeki/uniqueptr.h>

using namespace promeki;

// ============================================================================
// Basic push and pop
// ============================================================================

TEST_CASE("Queue_PushPop") {
        Queue<int> q;
        q.push(42);
        CHECK(q.size() == 1);
        auto [val, err] = q.pop();
        CHECK(err.isOk());
        CHECK(val == 42);
        CHECK(q.size() == 0);
}

TEST_CASE("Queue_PushPopOrdering") {
        Queue<int> q;
        q.push(1);
        q.push(2);
        q.push(3);
        CHECK(q.pop().first() == 1);
        CHECK(q.pop().first() == 2);
        CHECK(q.pop().first() == 3);
}

// ============================================================================
// Rvalue push (move semantics)
// ============================================================================

TEST_CASE("Queue_RvaluePush") {
        Queue<std::string> q;
        std::string        s = "hello";
        q.push(std::move(s));
        CHECK(q.size() == 1);
        auto [result, err] = q.pop();
        CHECK(err.isOk());
        CHECK(result == "hello");
}

// ============================================================================
// Emplace
// ============================================================================

TEST_CASE("Queue_Emplace") {
        // The element type itself is a std::pair (the queue is storing pairs);
        // that's a legitimate use of std::pair as a generic 2-tuple.  Only the
        // queue's own pop() return type was changed from std::pair<T, Error>
        // to Result<T> in the std::pair → Result migration.
        Queue<std::pair<int, std::string>> q;
        q.emplace(42, "answer");
        CHECK(q.size() == 1);
        auto [val, err] = q.pop();
        CHECK(err.isOk());
        CHECK(val.first == 42);
        CHECK(val.second == "answer");
}

// ============================================================================
// Vector push
// ============================================================================

TEST_CASE("Queue_ListPush") {
        Queue<int> q;
        List<int>  items = {10, 20, 30, 40};
        q.push(items);
        CHECK(q.size() == 4);
        CHECK(q.pop().first() == 10);
        CHECK(q.pop().first() == 20);
        CHECK(q.pop().first() == 30);
        CHECK(q.pop().first() == 40);
}

// ============================================================================
// Pop timeout
// ============================================================================

TEST_CASE("Queue_PopTimeout") {
        Queue<int> q;
        auto [val, err] = q.pop(50);
        CHECK(err == Error::Timeout);
}

TEST_CASE("Queue_PopTimeoutSuccess") {
        Queue<int> q;

        std::thread producer([&] { q.push(77); });

        auto [val, err] = q.pop(5000);
        CHECK(err.isOk());
        CHECK(val == 77);
        producer.join();
}

// ============================================================================
// tryPop
// ============================================================================

TEST_CASE("Queue_TryPopEmpty") {
        Queue<int> q;
        auto [val, err] = q.tryPop();
        CHECK(err == Error::Empty);
}

TEST_CASE("Queue_TryPopNonEmpty") {
        Queue<int> q;
        q.push(99);
        auto [val, err] = q.tryPop();
        CHECK(err.isOk());
        CHECK(val == 99);
        CHECK(q.size() == 0);
}

// ============================================================================
// peek and tryPeek
// ============================================================================

TEST_CASE("Queue_Peek") {
        Queue<int> q;
        q.push(7);
        auto [val, err] = q.peek();
        CHECK(err.isOk());
        CHECK(val == 7);
        // peek should not remove the item
        CHECK(q.size() == 1);
}

TEST_CASE("Queue_PeekTimeout") {
        Queue<int> q;
        auto [val, err] = q.peek(50);
        CHECK(err == Error::Timeout);
}

TEST_CASE("Queue_TryPeekEmpty") {
        Queue<int> q;
        auto [val, err] = q.tryPeek();
        CHECK(err == Error::Empty);
}

TEST_CASE("Queue_TryPeekNonEmpty") {
        Queue<int> q;
        q.push(55);
        auto [val, err] = q.tryPeek();
        CHECK(err.isOk());
        CHECK(val == 55);
        // Should not remove the item
        CHECK(q.size() == 1);
}

// ============================================================================
// size and clear
// ============================================================================

TEST_CASE("Queue_SizeEmpty") {
        Queue<int> q;
        CHECK(q.size() == 0);
}

TEST_CASE("Queue_Clear") {
        Queue<int> q;
        q.push(1);
        q.push(2);
        q.push(3);
        CHECK(q.size() == 3);
        q.clear();
        CHECK(q.size() == 0);
}

// ============================================================================
// waitForEmpty (backpressure)
// ============================================================================

TEST_CASE("Queue_WaitForEmptyAlreadyEmpty") {
        Queue<int> q;
        // Should return immediately when queue is already empty
        CHECK(q.waitForEmpty().isOk());
        CHECK(q.size() == 0);
}

TEST_CASE("Queue_WaitForEmptyTimeout") {
        Queue<int> q;
        q.push(1);
        // Queue is not empty and nothing is consuming, so this should time out
        CHECK(q.waitForEmpty(50) == Error::Timeout);
        // Item should still be in the queue
        CHECK(q.size() == 1);
}

TEST_CASE("Queue_WaitForEmptyTimeoutSuccess") {
        Queue<int> q;
        q.push(1);

        // Consumer drains after a short delay
        std::thread consumer([&] { q.pop(); });

        // Should succeed well within the timeout
        CHECK(q.waitForEmpty(5000).isOk());
        consumer.join();
}

TEST_CASE("Queue_WaitForEmptyBackpressure") {
        Queue<int>       q;
        constexpr int    count = 100;
        std::atomic<int> consumed{0};

        // Consumer thread drains the queue
        std::thread consumer([&] {
                for (int i = 0; i < count; i++) {
                        q.pop();
                        consumed.fetch_add(1, std::memory_order_relaxed);
                }
        });

        // Producer pushes items and waits for drain
        for (int i = 0; i < count; i++) {
                q.push(i);
        }
        q.waitForEmpty();

        // At this point all items have been popped
        CHECK(q.size() == 0);

        consumer.join();
        CHECK(consumed.load() == count);
}

// ============================================================================
// Multi-threaded producer/consumer
// ============================================================================

TEST_CASE("Queue_MultiThreadProducerConsumer") {
        Queue<int>             q;
        constexpr int          count = 1000;
        std::atomic<long long> sum{0};

        // Consumer sums all values
        std::thread consumer([&] {
                for (int i = 0; i < count; i++) {
                        auto [val, err] = q.pop();
                        sum.fetch_add(val, std::memory_order_relaxed);
                }
        });

        // Producer pushes 0..999
        for (int i = 0; i < count; i++) {
                q.push(i);
        }

        consumer.join();

        long long expected = (long long)(count - 1) * count / 2;
        CHECK(sum.load() == expected);
}

TEST_CASE("Queue_MultipleProducers") {
        Queue<int>             q;
        constexpr int          perProducer = 500;
        constexpr int          numProducers = 4;
        constexpr int          total = perProducer * numProducers;
        std::atomic<long long> sum{0};

        // Consumer
        std::thread consumer([&] {
                for (int i = 0; i < total; i++) {
                        auto [val, err] = q.pop();
                        sum.fetch_add(val, std::memory_order_relaxed);
                }
        });

        // Multiple producers each push 1s
        std::vector<std::thread> producers;
        for (int p = 0; p < numProducers; p++) {
                producers.emplace_back([&] {
                        for (int i = 0; i < perProducer; i++) {
                                q.push(1);
                        }
                });
        }

        for (auto &t : producers) t.join();
        consumer.join();

        CHECK(sum.load() == total);
}

// ============================================================================
// Move semantics on pop
// ============================================================================

TEST_CASE("Queue_MoveOnPop") {
        Queue<UniquePtr<int>> q;
        q.push(UniquePtr<int>::create(42));
        auto [val, err] = q.pop();
        CHECK(err.isOk());
        CHECK(val != nullptr);
        CHECK(*val == 42);
}

TEST_CASE("Queue_MoveOnTryPop") {
        Queue<UniquePtr<int>> q;
        q.push(UniquePtr<int>::create(99));
        auto [val, err] = q.tryPop();
        CHECK(err.isOk());
        CHECK(val != nullptr);
        CHECK(*val == 99);
}

TEST_CASE("Queue_IsEmpty") {
        Queue<int> q;
        CHECK(q.isEmpty());
        q.push(1);
        CHECK_FALSE(q.isEmpty());
        auto [val, err] = q.pop();
        CHECK(q.isEmpty());
}

// ============================================================================
// Bounded queue: setMaxSize / pushBlocking
// ============================================================================

TEST_CASE("Queue_SetMaxSizeDefaultsToUnbounded") {
        Queue<int> q;
        CHECK(q.maxSize() == 0);
}

TEST_CASE("Queue_SetMaxSizeRoundTrip") {
        Queue<int> q;
        q.setMaxSize(4);
        CHECK(q.maxSize() == 4);
        q.setMaxSize(0);
        CHECK(q.maxSize() == 0);
}

TEST_CASE("Queue_PlainPushIgnoresCap") {
        // The non-blocking overload is documented to ignore the cap —
        // it always inserts so existing callers do not change semantics.
        Queue<int> q;
        q.setMaxSize(2);
        q.push(1);
        q.push(2);
        q.push(3);
        CHECK(q.size() == 3);
}

TEST_CASE("Queue_PushBlockingFastPathWhenUnbounded") {
        Queue<int> q;
        // No cap → blocking push returns immediately.
        CHECK(q.pushBlocking(42).isOk());
        CHECK(q.size() == 1);
}

TEST_CASE("Queue_PushBlockingTimeoutWhenFull") {
        Queue<int> q;
        q.setMaxSize(2);
        CHECK(q.pushBlocking(1, 50).isOk());
        CHECK(q.pushBlocking(2, 50).isOk());
        // Now full — third push should time out.
        Error err = q.pushBlocking(3, 50);
        CHECK(err == Error::Timeout);
        CHECK(q.size() == 2);
}

TEST_CASE("Queue_PushBlockingResumesOnPop") {
        Queue<int> q;
        q.setMaxSize(1);
        CHECK(q.pushBlocking(7, 50).isOk());
        std::atomic<bool> done{false};
        Error             pushErr = Error::Timeout;
        std::thread       producer([&] {
                pushErr = q.pushBlocking(8, 5000);
                done.store(true, std::memory_order_release);
        });
        // Producer should be parked because the queue is full.  Drain
        // one slot — it must wake, push, and report Ok.
        auto [val, err] = q.pop();
        CHECK(err.isOk());
        CHECK(val == 7);
        producer.join();
        CHECK(done.load(std::memory_order_acquire));
        CHECK(pushErr.isOk());
        CHECK(q.size() == 1);
}

TEST_CASE("Queue_EmplaceBlockingHonorsCap") {
        Queue<std::pair<int, std::string>> q;
        q.setMaxSize(1);
        CHECK(q.emplaceBlocking(0, 42, "answer").isOk());
        // Full now — emplaceBlocking with 50ms timeout should report timeout.
        Error err = q.emplaceBlocking(50, 7, "lucky");
        CHECK(err == Error::Timeout);
        CHECK(q.size() == 1);
}

// ============================================================================
// cancelWaiters
// ============================================================================

TEST_CASE("Queue_CancelWaitersUnblocksPop") {
        Queue<int>        q;
        std::atomic<bool> done{false};
        Error             popErr = Error::Ok;
        std::thread       consumer([&] {
                auto r = q.pop();
                popErr = r.second();
                done.store(true, std::memory_order_release);
        });
        // Give the consumer a moment to park on the empty queue.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.cancelWaiters();
        consumer.join();
        CHECK(done.load(std::memory_order_acquire));
        CHECK(popErr == Error::Cancelled);
        CHECK(q.isCancelled());
}

TEST_CASE("Queue_CancelWaitersUnblocksProducer") {
        Queue<int> q;
        q.setMaxSize(1);
        CHECK(q.pushBlocking(1, 50).isOk());
        std::atomic<bool> done{false};
        Error             pushErr = Error::Ok;
        std::thread       producer([&] {
                pushErr = q.pushBlocking(2, 0);
                done.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.cancelWaiters();
        producer.join();
        CHECK(done.load(std::memory_order_acquire));
        CHECK(pushErr == Error::Cancelled);
}

TEST_CASE("Queue_CancelLatchAffectsLaterPop") {
        Queue<int> q;
        q.cancelWaiters();
        // Already cancelled — pop returns Cancelled immediately.
        auto [val, err] = q.pop();
        CHECK(err == Error::Cancelled);
}

TEST_CASE("Queue_PopDrainsBeforeCancelTakesEffect") {
        Queue<int> q;
        q.push(1);
        q.push(2);
        q.cancelWaiters();
        // Items in the queue are still drained — cancel only governs
        // blocking waits, not data already enqueued.
        CHECK(q.pop().first() == 1);
        CHECK(q.pop().first() == 2);
        // Now empty + cancelled — pop returns Cancelled.
        CHECK(q.pop().second() == Error::Cancelled);
}

TEST_CASE("Queue_ResetClearsCancelLatch") {
        Queue<int> q;
        q.cancelWaiters();
        CHECK(q.isCancelled());
        q.reset();
        CHECK_FALSE(q.isCancelled());
        // Reset re-arms — pushBlocking works again on the live queue.
        CHECK(q.pushBlocking(99, 50).isOk());
}

TEST_CASE("Queue_PushDropOldestUnboundedNeverDrops") {
        Queue<int> q;
        for (int i = 0; i < 100; ++i) {
                CHECK(q.pushDropOldest(i) == 0);
        }
        CHECK(q.size() == 100);
}

TEST_CASE("Queue_PushDropOldestEvictsOldestAtCapacity") {
        Queue<int> q;
        q.setMaxSize(3);
        CHECK(q.pushDropOldest(1) == 0);
        CHECK(q.pushDropOldest(2) == 0);
        CHECK(q.pushDropOldest(3) == 0);
        CHECK(q.size() == 3);
        // Fourth push evicts the front and inserts.
        CHECK(q.pushDropOldest(4) == 1);
        CHECK(q.size() == 3);
        CHECK(q.pop().first() == 2);
        CHECK(q.pop().first() == 3);
        CHECK(q.pop().first() == 4);
}

TEST_CASE("Queue_PushDropOldestRespectsShrunkCap") {
        Queue<int> q;
        q.setMaxSize(5);
        for (int i = 0; i < 5; ++i) q.pushDropOldest(i);
        CHECK(q.size() == 5);
        // Shrinking the cap below current size never drops mid-call;
        // it kicks in on the next pushDropOldest.
        q.setMaxSize(2);
        CHECK(q.size() == 5);
        // The new push must drop until size < cap, then push.  That
        // means evicting four entries (size 5 → 1 + push → 2).
        CHECK(q.pushDropOldest(99) == 4);
        CHECK(q.size() == 2);
        CHECK(q.pop().first() == 4);
        CHECK(q.pop().first() == 99);
}

TEST_CASE("Queue_PushDropOldestRvaluePreservesValue") {
        Queue<std::string> q;
        q.setMaxSize(2);
        q.pushDropOldest(std::string("first"));
        q.pushDropOldest(std::string("second"));
        const size_t dropped = q.pushDropOldest(std::string("third"));
        CHECK(dropped == 1);
        CHECK(q.pop().first() == std::string("second"));
        CHECK(q.pop().first() == std::string("third"));
}

TEST_CASE("Queue_PushDropOldestNoOpWhenCancelled") {
        Queue<int> q;
        q.setMaxSize(2);
        q.pushDropOldest(1);
        q.cancelWaiters();
        // Cancelled queue ignores drop-oldest pushes — neither
        // dropping nor pushing.  This way callers in their shutdown
        // path don't have to special-case the cancel.
        CHECK(q.pushDropOldest(2) == 0);
        CHECK(q.pushDropOldest(3) == 0);
        // The cancel latch lets remaining items drain via tryPop;
        // exactly one entry (the pre-cancel push) remains.
        CHECK(q.tryPop().first() == 1);
        CHECK(q.tryPop().second() == Error::Empty);
}

TEST_CASE("Queue_PushDropOldestRaceSafeConcurrentProducerConsumer") {
        // Single producer (drop-oldest) racing a single consumer.
        // The cap is intentionally smaller than the producer's burst
        // size so eviction must happen frequently; the consumer must
        // never observe a half-rotated queue (i.e. a missing front
        // or duplicate entry) since the lock is held for the full
        // drop-and-push window.
        Queue<int> q;
        q.setMaxSize(8);

        constexpr int kProduce = 5000;
        std::atomic<size_t> totalDropped{0};
        std::atomic<bool>   producerDone{false};
        std::atomic<bool>   consumerSawCorruption{false};

        std::thread producer([&] {
                for (int i = 0; i < kProduce; ++i) {
                        size_t d = q.pushDropOldest(i);
                        totalDropped.fetch_add(d, std::memory_order_relaxed);
                }
                producerDone.store(true, std::memory_order_release);
        });
        std::thread consumer([&] {
                int prev = -1;
                while (!producerDone.load(std::memory_order_acquire) || !q.isEmpty()) {
                        auto r = q.tryPop();
                        if (r.second() == Error::Empty) {
                                std::this_thread::yield();
                                continue;
                        }
                        // Strict monotonic increase: producer pushes
                        // 0..N-1 in order, drop-oldest only ever
                        // evicts head entries, so consumer always
                        // sees a strictly increasing prefix.
                        if (r.first() <= prev) {
                                consumerSawCorruption.store(true, std::memory_order_release);
                        }
                        prev = r.first();
                }
        });
        producer.join();
        consumer.join();
        CHECK_FALSE(consumerSawCorruption.load(std::memory_order_acquire));
        // Sum invariant: pushed = popped + dropped.  We pushed
        // exactly kProduce items; the consumer's prev tracks the
        // last popped value, so popped ≥ 1 (otherwise prev is -1).
        // We can't easily count popped without instrumenting
        // tryPop, but the corruption check above is the load-bearing
        // assertion — the dropped count is just a sanity probe.
        CHECK(totalDropped.load(std::memory_order_relaxed) <= kProduce);
}
