/**
 * @file      deque.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/deque.h>
#include <promeki/list.h>

using namespace promeki;

TEST_CASE("Deque: default construction") {
        Deque<int> d;
        CHECK(d.isEmpty());
        CHECK(d.size() == 0);
}

TEST_CASE("Deque: initializer list construction") {
        Deque<int> d = {1, 2, 3};
        CHECK(d.size() == 3);
        CHECK(d[0] == 1);
        CHECK(d[1] == 2);
        CHECK(d[2] == 3);
}

TEST_CASE("Deque: pushToFront and pushToBack") {
        Deque<int> d;
        d.pushToBack(2);
        d.pushToFront(1);
        d.pushToBack(3);
        CHECK(d.size() == 3);
        CHECK(d[0] == 1);
        CHECK(d[1] == 2);
        CHECK(d[2] == 3);
}

TEST_CASE("Deque: popFromFront and popFromBack") {
        Deque<int> d = {1, 2, 3};
        CHECK(d.popFromFront() == 1);
        CHECK(d.popFromBack() == 3);
        CHECK(d.size() == 1);
        CHECK(d[0] == 2);
}

TEST_CASE("Deque: front and back") {
        Deque<int> d = {10, 20, 30};
        CHECK(d.front() == 10);
        CHECK(d.back() == 30);
        d.front() = 99;
        CHECK(d.front() == 99);
}

TEST_CASE("Deque: at and operator[]") {
        Deque<int> d = {1, 2, 3};
        CHECK(d.at(1) == 2);
        CHECK(d[2] == 3);
        d[0] = 10;
        CHECK(d[0] == 10);
}

TEST_CASE("Deque: clear") {
        Deque<int> d = {1, 2, 3};
        d.clear();
        CHECK(d.isEmpty());
}

TEST_CASE("Deque: swap") {
        Deque<int> a = {1, 2};
        Deque<int> b = {3, 4, 5};
        a.swap(b);
        CHECK(a.size() == 3);
        CHECK(b.size() == 2);
        CHECK(a[0] == 3);
        CHECK(b[0] == 1);
}

TEST_CASE("Deque: forEach") {
        Deque<int> d = {1, 2, 3};
        int sum = 0;
        d.forEach([&](int v) { sum += v; });
        CHECK(sum == 6);
}

TEST_CASE("Deque: copy") {
        Deque<int> d1 = {1, 2};
        Deque<int> d2 = d1;
        d2.pushToBack(3);
        CHECK(d1.size() == 2);
        CHECK(d2.size() == 3);
}

TEST_CASE("Deque: move") {
        Deque<int> d1 = {1, 2};
        Deque<int> d2 = std::move(d1);
        CHECK(d2.size() == 2);
        CHECK(d2[0] == 1);
}

TEST_CASE("Deque: equality") {
        Deque<int> a = {1, 2, 3};
        Deque<int> b = {1, 2, 3};
        Deque<int> c = {1, 2, 4};
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Deque: reverse iterators") {
        Deque<int> d = {1, 2, 3};
        List<int> rev;
        for(auto it = d.rbegin(); it != d.rend(); ++it) {
                rev.pushToBack(*it);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[1] == 2);
        CHECK(rev[2] == 1);
}

TEST_CASE("Deque: range-based for") {
        Deque<int> d = {10, 20, 30};
        int sum = 0;
        for(const auto &v : d) sum += v;
        CHECK(sum == 60);
}

TEST_CASE("Deque: popFromFront on empty asserts") {
        Deque<int> d;
        CHECK_THROWS(d.popFromFront());
}

TEST_CASE("Deque: popFromBack on empty asserts") {
        Deque<int> d;
        CHECK_THROWS(d.popFromBack());
}
