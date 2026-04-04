/**
 * @file      map.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/map.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("Map: default construction") {
        Map<String, int> m;
        CHECK(m.isEmpty());
        CHECK(m.size() == 0);
}

TEST_CASE("Map: initializer list construction") {
        Map<String, int> m = {{"a", 1}, {"b", 2}, {"c", 3}};
        CHECK(m.size() == 3);
        CHECK(m["a"] == 1);
        CHECK(m["b"] == 2);
        CHECK(m["c"] == 3);
}

TEST_CASE("Map: insert and lookup") {
        Map<int, String> m;
        m.insert(1, "one");
        m.insert(2, "two");
        CHECK(m.size() == 2);
        CHECK(m[1] == "one");
        CHECK(m[2] == "two");
}

TEST_CASE("Map: operator[] inserts default") {
        Map<String, int> m;
        m["x"] = 42;
        CHECK(m.contains("x"));
        CHECK(m["x"] == 42);
}

TEST_CASE("Map: contains") {
        Map<int, int> m;
        m.insert(5, 50);
        CHECK(m.contains(5));
        CHECK_FALSE(m.contains(6));
}

TEST_CASE("Map: value with default") {
        Map<String, int> m = {{"a", 1}};
        CHECK(m.value("a") == 1);
        CHECK(m.value("b") == 0);
        CHECK(m.value("b", -1) == -1);
}

TEST_CASE("Map: find") {
        Map<int, String> m;
        m.insert(10, "ten");
        auto it = m.find(10);
        CHECK(it != m.end());
        CHECK(it->second == "ten");
        CHECK(m.find(99) == m.end());
}

TEST_CASE("Map: remove by key") {
        Map<String, int> m = {{"a", 1}, {"b", 2}};
        CHECK(m.remove("a"));
        CHECK_FALSE(m.contains("a"));
        CHECK(m.size() == 1);
        CHECK_FALSE(m.remove("z"));
}

TEST_CASE("Map: remove by iterator") {
        Map<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
        auto it = m.find(2);
        m.remove(it);
        CHECK_FALSE(m.contains(2));
        CHECK(m.size() == 2);
}

TEST_CASE("Map: clear") {
        Map<int, int> m = {{1, 1}, {2, 2}};
        m.clear();
        CHECK(m.isEmpty());
        CHECK(m.size() == 0);
}

TEST_CASE("Map: keys") {
        Map<int, String> m = {{3, "c"}, {1, "a"}, {2, "b"}};
        auto k = m.keys();
        CHECK(k.size() == 3);
        // std::map is ordered, so keys come out sorted
        CHECK(k[0] == 1);
        CHECK(k[1] == 2);
        CHECK(k[2] == 3);
}

TEST_CASE("Map: values") {
        Map<int, String> m = {{1, "a"}, {2, "b"}};
        auto v = m.values();
        CHECK(v.size() == 2);
        CHECK(v[0] == "a");
        CHECK(v[1] == "b");
}

TEST_CASE("Map: forEach") {
        Map<String, int> m = {{"x", 10}, {"y", 20}};
        int sum = 0;
        m.forEach([&](const String &, int v) { sum += v; });
        CHECK(sum == 30);
}

TEST_CASE("Map: copy") {
        Map<int, int> m1 = {{1, 10}, {2, 20}};
        Map<int, int> m2 = m1;
        m2.insert(3, 30);
        CHECK(m1.size() == 2);
        CHECK(m2.size() == 3);
}

TEST_CASE("Map: move") {
        Map<int, int> m1 = {{1, 10}};
        Map<int, int> m2 = std::move(m1);
        CHECK(m2.size() == 1);
        CHECK(m2[1] == 10);
}

TEST_CASE("Map: equality") {
        Map<int, int> a = {{1, 1}, {2, 2}};
        Map<int, int> b = {{1, 1}, {2, 2}};
        Map<int, int> c = {{1, 1}, {3, 3}};
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Map: insert overwrites") {
        Map<String, int> m;
        m.insert("key", 1);
        m.insert("key", 2);
        CHECK(m["key"] == 2);
        CHECK(m.size() == 1);
}

TEST_CASE("Map: range-based for") {
        Map<int, int> m = {{1, 10}, {2, 20}};
        int count = 0;
        for(const auto &[k, v] : m) {
                CHECK(v == k * 10);
                count++;
        }
        CHECK(count == 2);
}

TEST_CASE("Map: revBegin/revEnd") {
        Map<int, int> m = {{1, 10}, {2, 20}, {3, 30}};
        List<int> rev;
        for(auto it = m.revBegin(); it != m.revEnd(); ++it) {
                rev.pushToBack(it->first);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[1] == 2);
        CHECK(rev[2] == 1);
}

TEST_CASE("Map: constRevBegin/constRevEnd") {
        const Map<int, int> m = {{1, 10}, {2, 20}};
        List<int> rev;
        for(auto it = m.constRevBegin(); it != m.constRevEnd(); ++it) {
                rev.pushToBack(it->first);
        }
        CHECK(rev[0] == 2);
        CHECK(rev[1] == 1);
}

TEST_CASE("Map: swap") {
        Map<int, int> a = {{1, 10}};
        Map<int, int> b = {{2, 20}};
        a.swap(b);
        CHECK(a.contains(2));
        CHECK(b.contains(1));
}
