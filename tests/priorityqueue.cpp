/**
 * @file      priorityqueue.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/priorityqueue.h>

using namespace promeki;

TEST_CASE("PriorityQueue: default construction") {
        PriorityQueue<int> pq;
        CHECK(pq.isEmpty());
        CHECK(pq.size() == 0);
}

TEST_CASE("PriorityQueue: push and top") {
        PriorityQueue<int> pq;
        pq.push(10);
        pq.push(30);
        pq.push(20);
        CHECK(pq.size() == 3);
        CHECK(pq.top() == 30);
}

TEST_CASE("PriorityQueue: pop ordering") {
        PriorityQueue<int> pq;
        pq.push(5);
        pq.push(1);
        pq.push(3);
        pq.push(4);
        pq.push(2);
        CHECK(pq.pop() == 5);
        CHECK(pq.pop() == 4);
        CHECK(pq.pop() == 3);
        CHECK(pq.pop() == 2);
        CHECK(pq.pop() == 1);
        CHECK(pq.isEmpty());
}

TEST_CASE("PriorityQueue: custom comparator (min-heap)") {
        PriorityQueue<int, std::greater<int>> pq;
        pq.push(5);
        pq.push(1);
        pq.push(3);
        CHECK(pq.pop() == 1);
        CHECK(pq.pop() == 3);
        CHECK(pq.pop() == 5);
}

TEST_CASE("PriorityQueue: swap") {
        PriorityQueue<int> a;
        a.push(10);
        PriorityQueue<int> b;
        b.push(20);
        b.push(30);
        a.swap(b);
        CHECK(a.size() == 2);
        CHECK(b.size() == 1);
        CHECK(a.top() == 30);
        CHECK(b.top() == 10);
}

TEST_CASE("PriorityQueue: copy") {
        PriorityQueue<int> pq1;
        pq1.push(10);
        pq1.push(20);
        PriorityQueue<int> pq2 = pq1;
        pq2.push(30);
        CHECK(pq1.size() == 2);
        CHECK(pq2.size() == 3);
}

TEST_CASE("PriorityQueue: move") {
        PriorityQueue<int> pq1;
        pq1.push(42);
        PriorityQueue<int> pq2 = std::move(pq1);
        CHECK(pq2.size() == 1);
        CHECK(pq2.top() == 42);
}
