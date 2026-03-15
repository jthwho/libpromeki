/**
 * @file      array.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/array.h>

using namespace promeki;

TEST_CASE("Array: default construction") {
        Array<int, 4> a;
        CHECK(a.size() == 4);
}

TEST_CASE("Array: variadic construction") {
        Array<int, 3> a(1, 2, 3);
        CHECK(a[0] == 1);
        CHECK(a[1] == 2);
        CHECK(a[2] == 3);
}

TEST_CASE("Array: std::array construction") {
        std::array<int, 3> sa = {10, 20, 30};
        Array<int, 3> a(sa);
        CHECK(a[0] == 10);
        CHECK(a[1] == 20);
        CHECK(a[2] == 30);
}

TEST_CASE("Array: operator[]") {
        Array<int, 3> a(1, 2, 3);
        a[1] = 42;
        CHECK(a[1] == 42);
}

TEST_CASE("Array: const operator[]") {
        const Array<int, 3> a(1, 2, 3);
        CHECK(a[0] == 1);
}

TEST_CASE("Array: operator+=") {
        Array<int, 3> a(1, 2, 3);
        Array<int, 3> b(10, 20, 30);
        a += b;
        CHECK(a[0] == 11);
        CHECK(a[1] == 22);
        CHECK(a[2] == 33);
}

TEST_CASE("Array: operator-=") {
        Array<int, 3> a(10, 20, 30);
        Array<int, 3> b(1, 2, 3);
        a -= b;
        CHECK(a[0] == 9);
        CHECK(a[1] == 18);
        CHECK(a[2] == 27);
}

TEST_CASE("Array: operator*=") {
        Array<int, 3> a(2, 3, 4);
        Array<int, 3> b(5, 6, 7);
        a *= b;
        CHECK(a[0] == 10);
        CHECK(a[1] == 18);
        CHECK(a[2] == 28);
}

TEST_CASE("Array: operator/=") {
        Array<int, 3> a(10, 20, 30);
        Array<int, 3> b(2, 5, 10);
        a /= b;
        CHECK(a[0] == 5);
        CHECK(a[1] == 4);
        CHECK(a[2] == 3);
}

TEST_CASE("Array: scalar operator+=") {
        Array<int, 3> a(1, 2, 3);
        a += 10;
        CHECK(a[0] == 11);
        CHECK(a[1] == 12);
        CHECK(a[2] == 13);
}

TEST_CASE("Array: scalar operator*=") {
        Array<int, 3> a(1, 2, 3);
        a *= 3;
        CHECK(a[0] == 3);
        CHECK(a[1] == 6);
        CHECK(a[2] == 9);
}

TEST_CASE("Array: operator+ (array)") {
        Array<int, 3> a(1, 2, 3);
        Array<int, 3> b(10, 20, 30);
        auto c = a + b;
        CHECK(c[0] == 11);
        CHECK(c[1] == 22);
        CHECK(c[2] == 33);
}

TEST_CASE("Array: operator* (scalar)") {
        Array<int, 3> a(1, 2, 3);
        auto b = a * 5;
        CHECK(b[0] == 5);
        CHECK(b[1] == 10);
        CHECK(b[2] == 15);
}

TEST_CASE("Array: equality") {
        Array<int, 3> a(1, 2, 3);
        Array<int, 3> b(1, 2, 3);
        Array<int, 3> c(1, 2, 4);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Array: sum") {
        Array<int, 4> a(1, 2, 3, 4);
        CHECK(a.sum() == 10);
}

TEST_CASE("Array: mean") {
        Array<int, 4> a(2, 4, 6, 8);
        CHECK(a.mean() == doctest::Approx(5.0));
}

TEST_CASE("Array: data pointer") {
        Array<int, 3> a(10, 20, 30);
        int *p = a.data();
        CHECK(p[0] == 10);
        CHECK(p[1] == 20);
        CHECK(p[2] == 30);
}

TEST_CASE("Array: isZero") {
        Array<int, 3> zero;
        Array<int, 3> nonzero(0, 0, 1);
        CHECK(zero.isZero());
        CHECK_FALSE(nonzero.isZero());
}

TEST_CASE("Array: lerp") {
        Array<double, 2> a(0.0, 0.0);
        Array<double, 2> b(10.0, 20.0);
        auto mid = a.lerp(b, 0.5);
        CHECK(mid[0] == doctest::Approx(5.0));
        CHECK(mid[1] == doctest::Approx(10.0));
}

TEST_CASE("Array: isBetween") {
        Array<int, 3> val(5, 10, 15);
        Array<int, 3> min(0, 0, 0);
        Array<int, 3> max(20, 20, 20);
        Array<int, 3> oob(0, 0, 0);
        CHECK(val.isBetween(min, max));
        CHECK_FALSE(val.isBetween(min, oob));
}

TEST_CASE("Array: cross-size construction") {
        Array<int, 2> small(1, 2);
        Array<int, 4> large(small);
        CHECK(large[0] == 1);
        CHECK(large[1] == 2);
        CHECK(large[2] == 0);
        CHECK(large[3] == 0);
}

TEST_CASE("Array: assignment from scalar") {
        Array<int, 3> a;
        a = 42;
        CHECK(a[0] == 42);
        CHECK(a[1] == 42);
        CHECK(a[2] == 42);
}

TEST_CASE("Array: copy construction") {
        Array<int, 3> a(1, 2, 3);
        Array<int, 3> b(a);
        CHECK(b[0] == 1);
        CHECK(b[1] == 2);
        CHECK(b[2] == 3);
}
