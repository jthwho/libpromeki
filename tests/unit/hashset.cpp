/**
 * @file      hashset.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/hashset.h>

using namespace promeki;

TEST_CASE("HashSet: default construction") {
        HashSet<int> s;
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("HashSet: initializer list construction") {
        HashSet<int> s = {3, 1, 2};
        CHECK(s.size() == 3);
        CHECK(s.contains(1));
        CHECK(s.contains(2));
        CHECK(s.contains(3));
}

TEST_CASE("HashSet: insert") {
        HashSet<int> s;
        CHECK(s.insert(10));
        CHECK_FALSE(s.insert(10));
        CHECK(s.size() == 1);
}

TEST_CASE("HashSet: contains") {
        HashSet<int> s = {1, 2, 3};
        CHECK(s.contains(2));
        CHECK_FALSE(s.contains(99));
}

TEST_CASE("HashSet: remove by value") {
        HashSet<int> s = {1, 2, 3};
        CHECK(s.remove(2));
        CHECK_FALSE(s.contains(2));
        CHECK(s.size() == 2);
        CHECK_FALSE(s.remove(99));
}

TEST_CASE("HashSet: clear") {
        HashSet<int> s = {1, 2, 3};
        s.clear();
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("HashSet: toList") {
        HashSet<int> s = {3, 1, 2};
        auto         l = s.toList();
        CHECK(l.size() == 3);
        CHECK(l.contains(1));
        CHECK(l.contains(2));
        CHECK(l.contains(3));
}

TEST_CASE("HashSet: forEach") {
        HashSet<int> s = {1, 2, 3};
        int          sum = 0;
        s.forEach([&](int v) { sum += v; });
        CHECK(sum == 6);
}

TEST_CASE("HashSet: unite") {
        HashSet<int> a = {1, 2, 3};
        HashSet<int> b = {3, 4, 5};
        auto         u = a.unite(b);
        CHECK(u.size() == 5);
        CHECK(u.contains(1));
        CHECK(u.contains(5));
}

TEST_CASE("HashSet: intersect") {
        HashSet<int> a = {1, 2, 3};
        HashSet<int> b = {2, 3, 4};
        auto         i = a.intersect(b);
        CHECK(i.size() == 2);
        CHECK(i.contains(2));
        CHECK(i.contains(3));
        CHECK_FALSE(i.contains(1));
}

TEST_CASE("HashSet: subtract") {
        HashSet<int> a = {1, 2, 3};
        HashSet<int> b = {2, 3, 4};
        auto         s = a.subtract(b);
        CHECK(s.size() == 1);
        CHECK(s.contains(1));
}

TEST_CASE("HashSet: swap") {
        HashSet<int> a = {1, 2};
        HashSet<int> b = {3, 4};
        a.swap(b);
        CHECK(a.contains(3));
        CHECK(b.contains(1));
}

TEST_CASE("HashSet: copy") {
        HashSet<int> s1 = {1, 2};
        HashSet<int> s2 = s1;
        s2.insert(3);
        CHECK(s1.size() == 2);
        CHECK(s2.size() == 3);
}

TEST_CASE("HashSet: move") {
        HashSet<int> s1 = {1, 2};
        HashSet<int> s2 = std::move(s1);
        CHECK(s2.size() == 2);
        CHECK(s2.contains(1));
}

TEST_CASE("HashSet: equality") {
        HashSet<int> a = {1, 2, 3};
        HashSet<int> b = {1, 2, 3};
        HashSet<int> c = {1, 2, 4};
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("HashSet: duplicate insert ignored") {
        HashSet<int> s;
        s.insert(5);
        s.insert(5);
        s.insert(5);
        CHECK(s.size() == 1);
}
