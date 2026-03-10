/**
 * @file      line.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/line.h>

using namespace promeki;

TEST_CASE("Line2D: default construction") {
        Line2D l;
        CHECK(l.start().x() == 0);
        CHECK(l.start().y() == 0);
        CHECK(l.end().x() == 0);
        CHECK(l.end().y() == 0);
}

TEST_CASE("Line2D: construction with points") {
        Point2D s(1, 2);
        Point2D e(3, 4);
        Line2D l(s, e);
        CHECK(l.start().x() == 1);
        CHECK(l.start().y() == 2);
        CHECK(l.end().x() == 3);
        CHECK(l.end().y() == 4);
}

TEST_CASE("Line2D: move construction") {
        Line2D l(Point2D(10, 20), Point2D(30, 40));
        CHECK(l.start().x() == 10);
        CHECK(l.start().y() == 20);
        CHECK(l.end().x() == 30);
        CHECK(l.end().y() == 40);
}

TEST_CASE("Line2Df: float specialization") {
        Line2Df l(Point2Df(1.5f, 2.5f), Point2Df(3.5f, 4.5f));
        CHECK(l.start().x() == doctest::Approx(1.5f));
        CHECK(l.end().y() == doctest::Approx(4.5f));
}

TEST_CASE("Line3D: 3D line") {
        Point3D s(1, 2, 3);
        Point3D e(4, 5, 6);
        Line3D l(s, e);
        CHECK(l.start().x() == 1);
        CHECK(l.start().y() == 2);
        CHECK(l.start().z() == 3);
        CHECK(l.end().x() == 4);
        CHECK(l.end().y() == 5);
        CHECK(l.end().z() == 6);
}

TEST_CASE("Line2D: copy construction") {
        Line2D a(Point2D(1, 2), Point2D(3, 4));
        Line2D b(a);
        CHECK(b.start().x() == 1);
        CHECK(b.end().x() == 3);
}

TEST_CASE("Line2D: assignment") {
        Line2D a(Point2D(1, 2), Point2D(3, 4));
        Line2D b;
        b = a;
        CHECK(b.start().x() == 1);
        CHECK(b.end().y() == 4);
}
