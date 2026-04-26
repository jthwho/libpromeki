/**
 * @file      span.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/span.h>
#include <promeki/list.h>
#include <promeki/array.h>
#include <promeki/streamstring.h>
#include <promeki/textstream.h>

using namespace promeki;

TEST_CASE("Span: default construction") {
        Span<int> s;
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

TEST_CASE("Span: from pointer and size") {
        int       arr[] = {10, 20, 30};
        Span<int> s(arr, 3);
        CHECK(s.size() == 3);
        CHECK(s[0] == 10);
        CHECK(s[1] == 20);
        CHECK(s[2] == 30);
}

TEST_CASE("Span: from C array") {
        int       arr[] = {1, 2, 3, 4};
        Span<int> s(arr);
        CHECK(s.size() == 4);
        CHECK(s[0] == 1);
        CHECK(s[3] == 4);
}

TEST_CASE("Span: from List") {
        List<int> l = {5, 10, 15};
        Span<int> s(l);
        CHECK(s.size() == 3);
        CHECK(s[0] == 5);
        CHECK(s[2] == 15);
}

TEST_CASE("Span: from Array") {
        Array<int, 3> a(1, 2, 3);
        Span<int>     s(a);
        CHECK(s.size() == 3);
        CHECK(s[0] == 1);
        CHECK(s[2] == 3);
}

TEST_CASE("Span: front and back") {
        int       arr[] = {10, 20, 30};
        Span<int> s(arr, 3);
        CHECK(s.front() == 10);
        CHECK(s.back() == 30);
}

TEST_CASE("Span: data") {
        int       arr[] = {1, 2, 3};
        Span<int> s(arr, 3);
        CHECK(s.data() == arr);
}

TEST_CASE("Span: sizeBytes") {
        int       arr[] = {1, 2, 3};
        Span<int> s(arr, 3);
        CHECK(s.sizeBytes() == 3 * sizeof(int));
}

TEST_CASE("Span: subspan") {
        int       arr[] = {10, 20, 30, 40, 50};
        Span<int> s(arr, 5);
        auto      sub = s.subspan(1, 3);
        CHECK(sub.size() == 3);
        CHECK(sub[0] == 20);
        CHECK(sub[1] == 30);
        CHECK(sub[2] == 40);
}

TEST_CASE("Span: first and last") {
        int       arr[] = {1, 2, 3, 4, 5};
        Span<int> s(arr, 5);
        auto      f = s.first(2);
        CHECK(f.size() == 2);
        CHECK(f[0] == 1);
        CHECK(f[1] == 2);
        auto l = s.last(2);
        CHECK(l.size() == 2);
        CHECK(l[0] == 4);
        CHECK(l[1] == 5);
}

TEST_CASE("Span: modification through span") {
        int       arr[] = {1, 2, 3};
        Span<int> s(arr, 3);
        s[1] = 99;
        CHECK(arr[1] == 99);
}

TEST_CASE("Span: forEach") {
        int       arr[] = {1, 2, 3};
        Span<int> s(arr, 3);
        int       sum = 0;
        s.forEach([&](int v) { sum += v; });
        CHECK(sum == 6);
}

TEST_CASE("Span: range-based for") {
        int       arr[] = {10, 20, 30};
        Span<int> s(arr, 3);
        int       sum = 0;
        for (const auto &v : s) sum += v;
        CHECK(sum == 60);
}

TEST_CASE("Span: reverse iterators") {
        int       arr[] = {1, 2, 3};
        Span<int> s(arr, 3);
        List<int> rev;
        for (auto it = s.rbegin(); it != s.rend(); ++it) {
                rev.pushToBack(*it);
        }
        CHECK(rev[0] == 3);
        CHECK(rev[1] == 2);
        CHECK(rev[2] == 1);
}

TEST_CASE("Span: empty span operations") {
        Span<int> s;
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
        CHECK(s.sizeBytes() == 0);
        CHECK(s.data() == nullptr);
}

TEST_CASE("Span: const-Span subspan/first/last") {
        int             arr[] = {10, 20, 30, 40, 50};
        const Span<int> s(arr, 5);
        auto            sub = s.subspan(1, 3);
        CHECK(sub.size() == 3);
        CHECK(sub[0] == 20);
        auto f = s.first(2);
        CHECK(f.size() == 2);
        CHECK(f[0] == 10);
        auto l = s.last(2);
        CHECK(l.size() == 2);
        CHECK(l[0] == 40);
}

TEST_CASE("Span: converting constructor from Span<T> to Span<const T>") {
        int             arr[] = {1, 2, 3};
        Span<int>       mut(arr, 3);
        Span<const int> ro(mut);
        CHECK(ro.size() == 3);
        CHECK(ro[0] == 1);
        CHECK(ro[1] == 2);
        CHECK(ro[2] == 3);
        CHECK(ro.data() == arr);
}

TEST_CASE("Span: const Span front/back") {
        int             arr[] = {7, 8, 9};
        const Span<int> s(arr, 3);
        CHECK(s.front() == 7);
        CHECK(s.back() == 9);
}

TEST_CASE("Span: TextStream operator") {
        int          arr[] = {1, 2, 3};
        Span<int>    s(arr, 3);
        StreamString out;
        out.stream() << s << promeki::flush;
        CHECK(out.line() == "[1, 2, 3]");

        Span<int>    empty;
        StreamString out2;
        out2.stream() << empty << promeki::flush;
        CHECK(out2.line() == "[]");
}
