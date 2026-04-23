/**
 * @file      queue.cpp
 * @copyright Howard Logic. All rights reserved.
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
    std::string s = "hello";
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
    List<int> items = {10, 20, 30, 40};
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

    std::thread producer([&] {
        q.push(77);
    });

    auto [val, err] = q.pop(5000);
    CHECK(err.isOk());
    CHECK(val == 77);
    producer.join();
}

// ============================================================================
// popOrFail
// ============================================================================

TEST_CASE("Queue_PopOrFailEmpty") {
    Queue<int> q;
    int val = -1;
    CHECK(q.popOrFail(val) == false);
    CHECK(val == -1);
}

TEST_CASE("Queue_PopOrFailNonEmpty") {
    Queue<int> q;
    q.push(99);
    int val = -1;
    CHECK(q.popOrFail(val) == true);
    CHECK(val == 99);
    CHECK(q.size() == 0);
}

// ============================================================================
// peek and peekOrFail
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

TEST_CASE("Queue_PeekOrFailEmpty") {
    Queue<int> q;
    int val = -1;
    CHECK(q.peekOrFail(val) == false);
    CHECK(val == -1);
}

TEST_CASE("Queue_PeekOrFailNonEmpty") {
    Queue<int> q;
    q.push(55);
    int val = -1;
    CHECK(q.peekOrFail(val) == true);
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
    std::thread consumer([&] {
        q.pop();
    });

    // Should succeed well within the timeout
    CHECK(q.waitForEmpty(5000).isOk());
    consumer.join();
}

TEST_CASE("Queue_WaitForEmptyBackpressure") {
    Queue<int> q;
    constexpr int count = 100;
    std::atomic<int> consumed{0};

    // Consumer thread drains the queue
    std::thread consumer([&] {
        for(int i = 0; i < count; i++) {
            q.pop();
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Producer pushes items and waits for drain
    for(int i = 0; i < count; i++) {
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
    Queue<int> q;
    constexpr int count = 1000;
    std::atomic<long long> sum{0};

    // Consumer sums all values
    std::thread consumer([&] {
        for(int i = 0; i < count; i++) {
            auto [val, err] = q.pop();
            sum.fetch_add(val, std::memory_order_relaxed);
        }
    });

    // Producer pushes 0..999
    for(int i = 0; i < count; i++) {
        q.push(i);
    }

    consumer.join();

    long long expected = (long long)(count - 1) * count / 2;
    CHECK(sum.load() == expected);
}

TEST_CASE("Queue_MultipleProducers") {
    Queue<int> q;
    constexpr int perProducer = 500;
    constexpr int numProducers = 4;
    constexpr int total = perProducer * numProducers;
    std::atomic<long long> sum{0};

    // Consumer
    std::thread consumer([&] {
        for(int i = 0; i < total; i++) {
            auto [val, err] = q.pop();
            sum.fetch_add(val, std::memory_order_relaxed);
        }
    });

    // Multiple producers each push 1s
    std::vector<std::thread> producers;
    for(int p = 0; p < numProducers; p++) {
        producers.emplace_back([&] {
            for(int i = 0; i < perProducer; i++) {
                q.push(1);
            }
        });
    }

    for(auto &t : producers) t.join();
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

TEST_CASE("Queue_MoveOnPopOrFail") {
    Queue<UniquePtr<int>> q;
    q.push(UniquePtr<int>::create(99));
    UniquePtr<int> val;
    CHECK(q.popOrFail(val) == true);
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
