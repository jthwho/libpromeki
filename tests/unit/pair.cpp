/**
 * @file      pair.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pair.h>
#include <promeki/string.h>
#include <promeki/streamstring.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("Pair: default construction") {
        Pair<int, double> p;
        CHECK(p.first() == 0);
        CHECK(p.second() == 0.0);
}

TEST_CASE("Pair: value construction") {
        Pair<int, String> p(42, "hello");
        CHECK(p.first() == 42);
        CHECK(p.second() == "hello");
}

TEST_CASE("Pair: from std::pair") {
        std::pair<int, double> sp(10, 3.14);
        Pair<int, double>      p(sp);
        CHECK(p.first() == 10);
        CHECK(p.second() == doctest::Approx(3.14));
}

TEST_CASE("Pair: accessors") {
        Pair<int, int> p(1, 2);
        p.first() = 10;
        p.second() = 20;
        CHECK(p.first() == 10);
        CHECK(p.second() == 20);
}

TEST_CASE("Pair: setters") {
        Pair<int, int> p(1, 2);
        p.setFirst(100);
        p.setSecond(200);
        CHECK(p.first() == 100);
        CHECK(p.second() == 200);
}

TEST_CASE("Pair: toStdPair") {
        Pair<int, double> p(5, 2.5);
        const auto       &sp = p.toStdPair();
        CHECK(sp.first == 5);
        CHECK(sp.second == doctest::Approx(2.5));
}

TEST_CASE("Pair: comparison operators") {
        Pair<int, int> a(1, 2);
        Pair<int, int> b(1, 2);
        Pair<int, int> c(1, 3);
        Pair<int, int> d(2, 1);
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a < c);
        CHECK(a < d);
}

TEST_CASE("Pair: swap") {
        Pair<int, int> a(1, 2);
        Pair<int, int> b(3, 4);
        a.swap(b);
        CHECK(a.first() == 3);
        CHECK(a.second() == 4);
        CHECK(b.first() == 1);
        CHECK(b.second() == 2);
}

TEST_CASE("Pair: make factory") {
        auto p = Pair<int, double>::make(42, 3.14);
        CHECK(p.first() == 42);
        CHECK(p.second() == doctest::Approx(3.14));
}

TEST_CASE("Pair: structured bindings") {
        Pair<int, String> p(10, "test");
        auto [num, str] = p;
        CHECK(num == 10);
        CHECK(str == "test");
}

TEST_CASE("Pair: structured bindings mutable") {
        Pair<int, int> p(1, 2);
        auto &[a, b] = p;
        a = 10;
        b = 20;
        CHECK(p.first() == 10);
        CHECK(p.second() == 20);
}

TEST_CASE("Pair: copy") {
        Pair<int, int> p1(1, 2);
        Pair<int, int> p2 = p1;
        p2.setFirst(99);
        CHECK(p1.first() == 1);
        CHECK(p2.first() == 99);
}

TEST_CASE("Pair: move") {
        Pair<int, String> p1(42, "hello");
        Pair<int, String> p2 = std::move(p1);
        CHECK(p2.first() == 42);
        CHECK(p2.second() == "hello");
}

TEST_CASE("Pair: TextStream operator") {
        Pair<int, String> p(42, "hello");
        StreamString      out;
        out.stream() << p << promeki::flush;
        CHECK(out.line() == "(42, hello)");
}
