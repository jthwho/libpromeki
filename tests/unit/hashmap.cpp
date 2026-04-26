/**
 * @file      hashmap.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/hashmap.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("HashMap: default construction") {
        HashMap<int, int> m;
        CHECK(m.isEmpty());
        CHECK(m.size() == 0);
}

TEST_CASE("HashMap: initializer list construction") {
        HashMap<int, String> m = {{1, "one"}, {2, "two"}};
        CHECK(m.size() == 2);
        CHECK(m.contains(1));
        CHECK(m.contains(2));
}

TEST_CASE("HashMap: insert and contains") {
        HashMap<String, int> m;
        m.insert("hello", 42);
        CHECK(m.contains("hello"));
        CHECK_FALSE(m.contains("world"));
        CHECK(m.size() == 1);
}

TEST_CASE("HashMap: insert overwrites") {
        HashMap<int, int> m;
        m.insert(1, 10);
        m.insert(1, 20);
        CHECK(m.size() == 1);
        CHECK(m[1] == 20);
}

TEST_CASE("HashMap: operator[]") {
        HashMap<int, int> m;
        m[5] = 50;
        CHECK(m.contains(5));
        CHECK(m[5] == 50);
}

TEST_CASE("HashMap: value with default") {
        HashMap<int, int> m;
        m.insert(1, 100);
        CHECK(m.value(1) == 100);
        CHECK(m.value(2) == 0);
        CHECK(m.value(2, -1) == -1);
}

TEST_CASE("HashMap: remove by key") {
        HashMap<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
        CHECK(m.remove(2));
        CHECK_FALSE(m.contains(2));
        CHECK(m.size() == 2);
        CHECK_FALSE(m.remove(99));
}

TEST_CASE("HashMap: remove by iterator") {
        HashMap<int, int> m = {{1, 10}, {2, 20}};
        auto              it = m.find(1);
        m.remove(it);
        CHECK_FALSE(m.contains(1));
        CHECK(m.size() == 1);
}

TEST_CASE("HashMap: clear") {
        HashMap<int, int> m = {{1, 10}, {2, 20}};
        m.clear();
        CHECK(m.isEmpty());
        CHECK(m.size() == 0);
}

TEST_CASE("HashMap: keys and values") {
        HashMap<int, int> m = {{1, 10}, {2, 20}};
        auto              k = m.keys();
        auto              v = m.values();
        CHECK(k.size() == 2);
        CHECK(v.size() == 2);
        CHECK(k.contains(1));
        CHECK(k.contains(2));
        CHECK(v.contains(10));
        CHECK(v.contains(20));
}

TEST_CASE("HashMap: forEach") {
        HashMap<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
        int               sum = 0;
        m.forEach([&](int k, int v) { sum += k + v; });
        CHECK(sum == 66);
}

TEST_CASE("HashMap: find") {
        HashMap<int, int> m = {{5, 50}};
        auto              it = m.find(5);
        CHECK(it != m.end());
        CHECK(it->second == 50);
        CHECK(m.find(99) == m.end());
}

TEST_CASE("HashMap: swap") {
        HashMap<int, int> a = {{1, 10}};
        HashMap<int, int> b = {{2, 20}};
        a.swap(b);
        CHECK(a.contains(2));
        CHECK(b.contains(1));
}

TEST_CASE("HashMap: copy") {
        HashMap<int, int> m1 = {{1, 10}};
        HashMap<int, int> m2 = m1;
        m2.insert(2, 20);
        CHECK(m1.size() == 1);
        CHECK(m2.size() == 2);
}

TEST_CASE("HashMap: move") {
        HashMap<int, int> m1 = {{1, 10}};
        HashMap<int, int> m2 = std::move(m1);
        CHECK(m2.size() == 1);
        CHECK(m2.contains(1));
}

TEST_CASE("HashMap: equality") {
        HashMap<int, int> a = {{1, 10}, {2, 20}};
        HashMap<int, int> b = {{1, 10}, {2, 20}};
        HashMap<int, int> c = {{1, 10}, {3, 30}};
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("HashMap: range-based for") {
        HashMap<int, int> m = {{1, 10}, {2, 20}};
        int               sum = 0;
        for (const auto &[k, v] : m) sum += v;
        CHECK(sum == 30);
}
