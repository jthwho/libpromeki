/**
 * @file      rect.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rect.h>

using namespace promeki;

TEST_CASE("Rect: default construction") {
        Rect2Di32 r;
        CHECK(r.x() == 0);
        CHECK(r.y() == 0);
        CHECK(r.width() == 0);
        CHECK(r.height() == 0);
        CHECK(r.isEmpty());
        CHECK_FALSE(r.isValid());
}

TEST_CASE("Rect: construction with values") {
        Rect2Di32 r(10, 20, 100, 50);
        CHECK(r.x() == 10);
        CHECK(r.y() == 20);
        CHECK(r.width() == 100);
        CHECK(r.height() == 50);
        CHECK(r.isValid());
        CHECK_FALSE(r.isEmpty());
}

TEST_CASE("Rect: construction from Point and Size") {
        Point2Di32 pos(5, 10);
        Size2Di32 size(30, 40);
        Rect2Di32 r(pos, size);
        CHECK(r.x() == 5);
        CHECK(r.y() == 10);
        CHECK(r.width() == 30);
        CHECK(r.height() == 40);
}

TEST_CASE("Rect: corner accessors") {
        Rect2Di32 r(10, 20, 100, 50);
        CHECK(r.topLeft() == Point2Di32(10, 20));
        CHECK(r.topRight() == Point2Di32(110, 20));
        CHECK(r.bottomLeft() == Point2Di32(10, 70));
        CHECK(r.bottomRight() == Point2Di32(110, 70));
        CHECK(r.center() == Point2Di32(60, 45));
}

TEST_CASE("Rect: contains point") {
        Rect2Di32 r(0, 0, 10, 10);
        CHECK(r.contains(Point2Di32(5, 5)));
        CHECK(r.contains(Point2Di32(0, 0)));
        CHECK_FALSE(r.contains(Point2Di32(10, 10)));
        CHECK_FALSE(r.contains(Point2Di32(-1, 5)));
}

TEST_CASE("Rect: contains rect") {
        Rect2Di32 outer(0, 0, 100, 100);
        Rect2Di32 inner(10, 10, 20, 20);
        Rect2Di32 partial(90, 90, 20, 20);
        CHECK(outer.contains(inner));
        CHECK_FALSE(outer.contains(partial));
}

TEST_CASE("Rect: intersects") {
        Rect2Di32 a(0, 0, 10, 10);
        Rect2Di32 b(5, 5, 10, 10);
        Rect2Di32 c(20, 20, 10, 10);
        CHECK(a.intersects(b));
        CHECK_FALSE(a.intersects(c));
}

TEST_CASE("Rect: intersected") {
        Rect2Di32 a(0, 0, 10, 10);
        Rect2Di32 b(5, 5, 10, 10);
        Rect2Di32 result = a.intersected(b);
        CHECK(result.x() == 5);
        CHECK(result.y() == 5);
        CHECK(result.width() == 5);
        CHECK(result.height() == 5);
}

TEST_CASE("Rect: intersected empty") {
        Rect2Di32 a(0, 0, 10, 10);
        Rect2Di32 b(20, 20, 10, 10);
        Rect2Di32 result = a.intersected(b);
        CHECK(result.isEmpty());
}

TEST_CASE("Rect: united") {
        Rect2Di32 a(0, 0, 10, 10);
        Rect2Di32 b(5, 5, 10, 10);
        Rect2Di32 result = a.united(b);
        CHECK(result.x() == 0);
        CHECK(result.y() == 0);
        CHECK(result.width() == 15);
        CHECK(result.height() == 15);
}

TEST_CASE("Rect: adjusted") {
        Rect2Di32 r(10, 20, 100, 50);
        Rect2Di32 adj = r.adjusted(1, 2, -1, -2);
        CHECK(adj.x() == 11);
        CHECK(adj.y() == 22);
        CHECK(adj.width() == 98);
        CHECK(adj.height() == 46);
}

TEST_CASE("Rect: translated") {
        Rect2Di32 r(10, 20, 100, 50);
        Rect2Di32 t = r.translated(5, -5);
        CHECK(t.x() == 15);
        CHECK(t.y() == 15);
        CHECK(t.width() == 100);
        CHECK(t.height() == 50);
}

TEST_CASE("Rect: equality") {
        Rect2Di32 a(1, 2, 3, 4);
        Rect2Di32 b(1, 2, 3, 4);
        Rect2Di32 c(1, 2, 3, 5);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Rect2Df: floating point") {
        Rect2Df r(1.5f, 2.5f, 10.0f, 20.0f);
        CHECK(r.x() == doctest::Approx(1.5f));
        CHECK(r.y() == doctest::Approx(2.5f));
        CHECK(r.width() == doctest::Approx(10.0f));
        CHECK(r.height() == doctest::Approx(20.0f));
}
