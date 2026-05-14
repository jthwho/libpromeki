/**
 * @file      optional.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <optional>
#include <utility>
#include <doctest/doctest.h>
#include <promeki/optional.h>

using namespace promeki;

TEST_CASE("Optional: default-constructed is empty") {
        Optional<int> a;
        CHECK(!a);
        CHECK(!a.hasValue());
        CHECK(a == std::nullopt);
}

TEST_CASE("Optional: nullopt construction is empty") {
        Optional<int> a(std::nullopt);
        CHECK(!a);
}

TEST_CASE("Optional: value construction holds the value") {
        Optional<int> a(42);
        CHECK(static_cast<bool>(a));
        CHECK(a.hasValue());
        CHECK(a.value() == 42);
        CHECK(*a == 42);
}

TEST_CASE("Optional: valueOr fallback when empty") {
        Optional<int> a;
        CHECK(a.valueOr(-1) == -1);
}

TEST_CASE("Optional: valueOr returns value when present") {
        Optional<int> a(7);
        CHECK(a.valueOr(-1) == 7);
}

TEST_CASE("Optional: assign value") {
        Optional<int> a;
        a = 9;
        CHECK(a);
        CHECK(a.value() == 9);
}

TEST_CASE("Optional: reset() clears value") {
        Optional<int> a(5);
        REQUIRE(a);
        a.reset();
        CHECK(!a);
}

TEST_CASE("Optional: nullopt assignment clears") {
        Optional<int> a(5);
        a = std::nullopt;
        CHECK(!a);
}

TEST_CASE("Optional: copy preserves state") {
        Optional<int> a(11);
        Optional<int> b = a;
        CHECK(a.value() == 11);
        CHECK(b.value() == 11);
}

TEST_CASE("Optional: move transfers state") {
        Optional<int> a(11);
        Optional<int> b = std::move(a);
        CHECK(b.value() == 11);
}

TEST_CASE("Optional: equality - both empty") {
        Optional<int> a;
        Optional<int> b;
        CHECK(a == b);
}

TEST_CASE("Optional: equality - both holding equal values") {
        Optional<int> a(5);
        Optional<int> b(5);
        CHECK(a == b);
}

TEST_CASE("Optional: inequality - one empty, one held") {
        Optional<int> a;
        Optional<int> b(5);
        CHECK(a != b);
}

TEST_CASE("Optional: emplace constructs in place") {
        struct Foo {
                        int a;
                        int b;
                        Foo(int x, int y) : a(x), b(y) {}
        };
        Optional<Foo> o;
        o.emplace(3, 4);
        REQUIRE(o);
        CHECK(o->a == 3);
        CHECK(o->b == 4);
}

TEST_CASE("Optional: arrow operator") {
        struct Foo {
                        int v;
        };
        Optional<Foo> o(Foo{99});
        CHECK(o->v == 99);
}

TEST_CASE("Optional: toStdOptional round-trip") {
        Optional<int>      a(7);
        std::optional<int> s = a.toStdOptional();
        CHECK(s.has_value());
        CHECK(s.value() == 7);
}

TEST_CASE("Optional: construct from std::optional") {
        std::optional<int> s(13);
        Optional<int>      o(s);
        CHECK(o);
        CHECK(o.value() == 13);
}

TEST_CASE("Optional: swap exchanges values") {
        Optional<int> a(1);
        Optional<int> b(2);
        a.swap(b);
        CHECK(a.value() == 2);
        CHECK(b.value() == 1);
}

TEST_CASE("Optional: bool conversion via empty/non-empty") {
        Optional<int> empty;
        Optional<int> held(0);  // Even 0 should be "present"
        CHECK(!empty);
        CHECK(held);
}
