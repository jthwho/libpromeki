/**
 * @file      stack.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/stack.h>

using namespace promeki;

TEST_CASE("Stack: default construction") {
        Stack<int> s;
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("Stack: push and top") {
        Stack<int> s;
        s.push(10);
        s.push(20);
        CHECK(s.size() == 2);
        CHECK(s.top() == 20);
}

TEST_CASE("Stack: pop") {
        Stack<int> s;
        s.push(1);
        s.push(2);
        s.push(3);
        CHECK(s.pop() == 3);
        CHECK(s.pop() == 2);
        CHECK(s.pop() == 1);
        CHECK(s.isEmpty());
}

TEST_CASE("Stack: LIFO order") {
        Stack<int> s;
        for (int i = 0; i < 5; ++i) s.push(i);
        for (int i = 4; i >= 0; --i) {
                CHECK(s.pop() == i);
        }
}

TEST_CASE("Stack: constTop") {
        Stack<int> s;
        s.push(42);
        const auto &cs = s;
        CHECK(cs.constTop() == 42);
}

TEST_CASE("Stack: mutable top") {
        Stack<int> s;
        s.push(10);
        s.top() = 99;
        CHECK(s.top() == 99);
}

TEST_CASE("Stack: clear") {
        Stack<int> s;
        s.push(1);
        s.push(2);
        s.clear();
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("Stack: swap") {
        Stack<int> a;
        a.push(1);
        Stack<int> b;
        b.push(2);
        b.push(3);
        a.swap(b);
        CHECK(a.size() == 2);
        CHECK(b.size() == 1);
        CHECK(a.top() == 3);
        CHECK(b.top() == 1);
}

TEST_CASE("Stack: copy") {
        Stack<int> s1;
        s1.push(10);
        Stack<int> s2 = s1;
        s2.push(20);
        CHECK(s1.size() == 1);
        CHECK(s2.size() == 2);
}

TEST_CASE("Stack: move") {
        Stack<int> s1;
        s1.push(10);
        Stack<int> s2 = std::move(s1);
        CHECK(s2.size() == 1);
        CHECK(s2.top() == 10);
}

TEST_CASE("Stack: equality") {
        Stack<int> a;
        a.push(1);
        a.push(2);
        Stack<int> b;
        b.push(1);
        b.push(2);
        Stack<int> c;
        c.push(1);
        c.push(3);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Stack: pop on empty asserts") {
        Stack<int> s;
        CHECK_THROWS(s.pop());
}
