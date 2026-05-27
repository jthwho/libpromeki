/**
 * @file      function.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <functional>
#include <memory>
#include <doctest/doctest.h>
#include <promeki/function.h>

using namespace promeki;

namespace {

int freeAdd(int a, int b) { return a + b; }

struct Functor {
                int operator()(int x) const { return x * 3; }
};

} // namespace

TEST_CASE("Function: default-constructed is empty") {
        Function<int(int, int)> f;
        CHECK(!f);
        CHECK(f.isNull());
}

TEST_CASE("Function: nullptr construction is empty") {
        Function<int(int, int)> f(nullptr);
        CHECK(!f);
}

TEST_CASE("Function: nullptr assignment clears") {
        Function<int(int)> f = [](int x) { return x + 1; };
        REQUIRE(f);
        f = nullptr;
        CHECK(!f);
}

TEST_CASE("Function: bind a lambda and invoke") {
        Function<int(int, int)> f = [](int a, int b) { return a + b; };
        CHECK(static_cast<bool>(f));
        CHECK(f(2, 3) == 5);
}

TEST_CASE("Function: bind a free function") {
        Function<int(int, int)> f = freeAdd;
        CHECK(f(4, 5) == 9);
}

TEST_CASE("Function: bind a functor object") {
        Function<int(int)> f = Functor{};
        CHECK(f(4) == 12);
}

TEST_CASE("Function: copy preserves target") {
        Function<int(int)> a = [](int x) { return x + 10; };
        Function<int(int)> b = a;
        CHECK(a(1) == 11);
        CHECK(b(1) == 11);
}

TEST_CASE("Function: move transfers target") {
        Function<int(int)> a = [](int x) { return x + 10; };
        Function<int(int)> b = std::move(a);
        CHECK(b(1) == 11);
}

TEST_CASE("Function: assignment of a new callable replaces the old one") {
        Function<int(int)> f = [](int x) { return x + 1; };
        f                    = [](int x) { return x * 100; };
        CHECK(f(2) == 200);
}

TEST_CASE("Function: void return type") {
        int                count = 0;
        Function<void(int)> f    = [&count](int n) { count += n; };
        f(3);
        f(4);
        CHECK(count == 7);
}

TEST_CASE("Function: no-argument signature") {
        Function<int()> f = [] { return 42; };
        CHECK(f() == 42);
}

TEST_CASE("Function: clear() empties the target") {
        Function<int(int)> f = [](int x) { return x; };
        REQUIRE(f);
        f.clear();
        CHECK(!f);
}

TEST_CASE("Function: swap exchanges targets") {
        Function<int(int)> a = [](int x) { return x + 1; };
        Function<int(int)> b = [](int x) { return x - 1; };
        a.swap(b);
        CHECK(a(5) == 4);
        CHECK(b(5) == 6);
}

TEST_CASE("Function: toStdFunction round-trip") {
        Function<int(int)>      a = [](int x) { return x + 5; };
        std::function<int(int)> s = a.toStdFunction();
        CHECK(s(1) == 6);
}

TEST_CASE("Function: construct from std::function") {
        std::function<int(int)> s = [](int x) { return x * 2; };
        Function<int(int)>      f(s);
        CHECK(f(3) == 6);
}

TEST_CASE("Function: empty invocation throws bad_function_call") {
        Function<int()> f;
        CHECK_THROWS_AS(f(), std::bad_function_call);
}

TEST_CASE("Function: by-reference argument forwarding") {
        Function<void(int &)> f = [](int &x) { x *= 2; };
        int                   v = 7;
        f(v);
        CHECK(v == 14);
}

TEST_CASE("Function: move-only argument types") {
        Function<int(std::unique_ptr<int>)> f = [](std::unique_ptr<int> p) { return *p + 1; };
        auto                                p = std::make_unique<int>(41);
        CHECK(f(std::move(p)) == 42);
}
