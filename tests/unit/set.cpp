/**
 * @file      set.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/set.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("Set: default construction") {
        Set<int> s;
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("Set: initializer list construction") {
        Set<int> s = {3, 1, 2};
        CHECK(s.size() == 3);
        CHECK(s.contains(1));
        CHECK(s.contains(2));
        CHECK(s.contains(3));
}

TEST_CASE("Set: insert") {
        Set<int> s;
        auto [it1, ok1] = s.insert(10);
        CHECK(ok1);
        CHECK(*it1 == 10);
        auto [it2, ok2] = s.insert(10);
        CHECK_FALSE(ok2);
        CHECK(s.size() == 1);
}

TEST_CASE("Set: contains") {
        Set<String> s;
        s.insert("hello");
        CHECK(s.contains("hello"));
        CHECK_FALSE(s.contains("world"));
}

TEST_CASE("Set: find") {
        Set<int> s = {5, 10, 15};
        auto     it = s.find(10);
        CHECK(it != s.end());
        CHECK(*it == 10);
        CHECK(s.find(99) == s.end());
}

TEST_CASE("Set: remove by value") {
        Set<int> s = {1, 2, 3};
        CHECK(s.remove(2));
        CHECK_FALSE(s.contains(2));
        CHECK(s.size() == 2);
        CHECK_FALSE(s.remove(99));
}

TEST_CASE("Set: remove by iterator") {
        Set<int> s = {10, 20, 30};
        auto     it = s.find(20);
        s.remove(it);
        CHECK_FALSE(s.contains(20));
        CHECK(s.size() == 2);
}

TEST_CASE("Set: clear") {
        Set<int> s = {1, 2, 3};
        s.clear();
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("Set: toList") {
        Set<int> s = {3, 1, 2};
        auto     l = s.toList();
        CHECK(l.size() == 3);
        // std::set is ordered, so elements come out sorted
        CHECK(l[0] == 1);
        CHECK(l[1] == 2);
        CHECK(l[2] == 3);
}

TEST_CASE("Set: forEach") {
        Set<int> s = {1, 2, 3};
        int      sum = 0;
        s.forEach([&](int v) { sum += v; });
        CHECK(sum == 6);
}

TEST_CASE("Set: copy") {
        Set<int> s1 = {1, 2, 3};
        Set<int> s2 = s1;
        s2.insert(4);
        CHECK(s1.size() == 3);
        CHECK(s2.size() == 4);
}

TEST_CASE("Set: move") {
        Set<int> s1 = {1, 2};
        Set<int> s2 = std::move(s1);
        CHECK(s2.size() == 2);
        CHECK(s2.contains(1));
        CHECK(s2.contains(2));
}

TEST_CASE("Set: equality") {
        Set<int> a = {1, 2, 3};
        Set<int> b = {1, 2, 3};
        Set<int> c = {1, 2, 4};
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Set: lowerBound and upperBound") {
        Set<int> s = {10, 20, 30, 40, 50};
        auto     lb = s.lowerBound(25);
        CHECK(*lb == 30);
        auto ub = s.upperBound(30);
        CHECK(*ub == 40);
}

TEST_CASE("Set: duplicate insert ignored") {
        Set<int> s;
        s.insert(5);
        s.insert(5);
        s.insert(5);
        CHECK(s.size() == 1);
}

TEST_CASE("Set: range-based for") {
        Set<int>  s = {3, 1, 2};
        List<int> collected;
        for (const auto &v : s) {
                collected.pushToBack(v);
        }
        CHECK(collected.size() == 3);
        CHECK(collected[0] == 1);
        CHECK(collected[1] == 2);
        CHECK(collected[2] == 3);
}

TEST_CASE("Set: reverse iterators") {
        Set<int>  s = {1, 2, 3};
        List<int> rev;
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
                rev.pushToBack(*it);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[1] == 2);
        CHECK(rev[2] == 1);
}

TEST_CASE("Set: revBegin/revEnd aliases") {
        Set<int>  s = {1, 2, 3};
        List<int> rev;
        for (auto it = s.revBegin(); it != s.revEnd(); ++it) {
                rev.pushToBack(*it);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[2] == 1);
}

TEST_CASE("Set: constRevBegin/constRevEnd") {
        const Set<int> s = {1, 2, 3};
        List<int>      rev;
        for (auto it = s.constRevBegin(); it != s.constRevEnd(); ++it) {
                rev.pushToBack(*it);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[2] == 1);
}

TEST_CASE("Set: swap") {
        Set<int> a = {1, 2};
        Set<int> b = {3, 4, 5};
        a.swap(b);
        CHECK(a.size() == 3);
        CHECK(b.size() == 2);
        CHECK(a.contains(3));
        CHECK(b.contains(1));
}

TEST_CASE("Set: unite") {
        Set<int> a = {1, 2, 3};
        Set<int> b = {3, 4, 5};
        auto     u = a.unite(b);
        CHECK(u.size() == 5);
        CHECK(u.contains(1));
        CHECK(u.contains(5));
}

TEST_CASE("Set: intersect") {
        Set<int> a = {1, 2, 3};
        Set<int> b = {2, 3, 4};
        auto     i = a.intersect(b);
        CHECK(i.size() == 2);
        CHECK(i.contains(2));
        CHECK(i.contains(3));
        CHECK_FALSE(i.contains(1));
}

TEST_CASE("Set: subtract") {
        Set<int> a = {1, 2, 3};
        Set<int> b = {2, 3, 4};
        auto     s = a.subtract(b);
        CHECK(s.size() == 1);
        CHECK(s.contains(1));
}
