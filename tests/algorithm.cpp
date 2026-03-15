/**
 * @file      algorithm.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/algorithm.h>
#include <promeki/core/list.h>
#include <promeki/core/set.h>
#include <promeki/core/deque.h>
#include <promeki/core/string.h>

using namespace promeki;

TEST_CASE("Algorithm: sorted with List") {
        List<int> l = {3, 1, 4, 1, 5, 9};
        List<int> s = sorted(l);
        CHECK(s[0] == 1);
        CHECK(s[1] == 1);
        CHECK(s[2] == 3);
        CHECK(s[3] == 4);
        CHECK(s[4] == 5);
        CHECK(s[5] == 9);
        // Original unchanged
        CHECK(l[0] == 3);
}

TEST_CASE("Algorithm: sorted with custom comparator") {
        List<int> l = {3, 1, 4, 1, 5};
        List<int> s = sorted(l, [](int a, int b) { return a > b; });
        CHECK(s[0] == 5);
        CHECK(s[1] == 4);
        CHECK(s[2] == 3);
}

TEST_CASE("Algorithm: filtered") {
        List<int> l = {1, 2, 3, 4, 5, 6};
        List<int> evens = filtered(l, [](int x) { return x % 2 == 0; });
        CHECK(evens.size() == 3);
        CHECK(evens[0] == 2);
        CHECK(evens[1] == 4);
        CHECK(evens[2] == 6);
}

TEST_CASE("Algorithm: mapped") {
        List<int> l = {1, 2, 3};
        auto doubled = mapped(l, [](int x) { return x * 2; });
        CHECK(doubled.size() == 3);
        CHECK(doubled[0] == 2);
        CHECK(doubled[1] == 4);
        CHECK(doubled[2] == 6);
}

TEST_CASE("Algorithm: mapped type transformation") {
        List<int> l = {1, 2, 3};
        auto strings = mapped(l, [](int x) { return String::sprintf("%d", x); });
        CHECK(strings.size() == 3);
        CHECK(strings[0] == "1");
}

TEST_CASE("Algorithm: allOf") {
        List<int> l = {2, 4, 6, 8};
        CHECK(allOf(l, [](int x) { return x % 2 == 0; }));
        CHECK_FALSE(allOf(l, [](int x) { return x > 5; }));
}

TEST_CASE("Algorithm: anyOf") {
        List<int> l = {1, 3, 5, 6};
        CHECK(anyOf(l, [](int x) { return x % 2 == 0; }));
        CHECK_FALSE(anyOf(l, [](int x) { return x > 10; }));
}

TEST_CASE("Algorithm: noneOf") {
        List<int> l = {1, 3, 5, 7};
        CHECK(noneOf(l, [](int x) { return x % 2 == 0; }));
        CHECK_FALSE(noneOf(l, [](int x) { return x < 5; }));
}

TEST_CASE("Algorithm: forEach") {
        List<int> l = {1, 2, 3};
        int sum = 0;
        forEach(l, [&sum](int x) { sum += x; });
        CHECK(sum == 6);
}

TEST_CASE("Algorithm: accumulate") {
        List<int> l = {1, 2, 3, 4, 5};
        int sum = accumulate(l, 0, [](int a, int b) { return a + b; });
        CHECK(sum == 15);
}

TEST_CASE("Algorithm: minElement") {
        List<int> l = {5, 2, 8, 1, 9};
        auto it = minElement(l);
        CHECK(*it == 1);
}

TEST_CASE("Algorithm: maxElement") {
        List<int> l = {5, 2, 8, 1, 9};
        auto it = maxElement(l);
        CHECK(*it == 9);
}

TEST_CASE("Algorithm: contains with List") {
        List<int> l = {1, 2, 3, 4, 5};
        CHECK(contains(l, 3));
        CHECK_FALSE(contains(l, 6));
}

TEST_CASE("Algorithm: contains with Deque") {
        Deque<int> d = {10, 20, 30};
        CHECK(contains(d, 20));
        CHECK_FALSE(contains(d, 40));
}

TEST_CASE("Algorithm: allOf/anyOf/noneOf with empty container") {
        List<int> empty;
        CHECK(allOf(empty, [](int) { return false; }));
        CHECK_FALSE(anyOf(empty, [](int) { return true; }));
        CHECK(noneOf(empty, [](int) { return true; }));
}

TEST_CASE("Algorithm: accumulate with Deque") {
        Deque<int> d = {1, 2, 3};
        int product = accumulate(d, 1, [](int a, int b) { return a * b; });
        CHECK(product == 6);
}

TEST_CASE("Algorithm: sorted with empty container") {
        List<int> empty;
        List<int> result = sorted(empty);
        CHECK(result.isEmpty());
}

TEST_CASE("Algorithm: sorted with Deque") {
        Deque<int> d = {5, 3, 1, 4, 2};
        Deque<int> s = sorted(d);
        CHECK(s[0] == 1);
        CHECK(s[4] == 5);
}

TEST_CASE("Algorithm: filtered with no matches") {
        List<int> l = {1, 3, 5, 7};
        List<int> result = filtered(l, [](int x) { return x % 2 == 0; });
        CHECK(result.isEmpty());
}

TEST_CASE("Algorithm: filtered with empty container") {
        List<int> empty;
        List<int> result = filtered(empty, [](int) { return true; });
        CHECK(result.isEmpty());
}

TEST_CASE("Algorithm: mapped with empty container") {
        List<int> empty;
        auto result = mapped(empty, [](int x) { return x * 2; });
        CHECK(result.isEmpty());
}

TEST_CASE("Algorithm: mapped with Deque") {
        Deque<int> d = {1, 2, 3};
        auto result = mapped(d, [](int x) { return x * 10; });
        CHECK(result.size() == 3);
        CHECK(result[0] == 10);
        CHECK(result[2] == 30);
}

TEST_CASE("Algorithm: forEach with Deque") {
        Deque<int> d = {10, 20, 30};
        int sum = 0;
        forEach(d, [&sum](int x) { sum += x; });
        CHECK(sum == 60);
}

TEST_CASE("Algorithm: minElement and maxElement with Deque") {
        Deque<int> d = {3, 1, 4, 1, 5};
        CHECK(*minElement(d) == 1);
        CHECK(*maxElement(d) == 5);
}

TEST_CASE("Algorithm: sorted preserves original") {
        Deque<int> d = {3, 1, 2};
        Deque<int> s = sorted(d);
        CHECK(d[0] == 3); // original unchanged
        CHECK(s[0] == 1);
}
