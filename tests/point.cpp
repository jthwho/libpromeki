/**
 * @file      point.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/point.h>

using namespace promeki;

TEST_CASE("Point: default construction") {
        Point2D p;
        CHECK(p.x() == 0);
        CHECK(p.y() == 0);
}

TEST_CASE("Point: construction with values") {
        Point2D p(3, 7);
        CHECK(p.x() == 3);
        CHECK(p.y() == 7);
}

TEST_CASE("Point: setX and setY") {
        Point2D p;
        p.setX(10);
        p.setY(20);
        CHECK(p.x() == 10);
        CHECK(p.y() == 20);
}

TEST_CASE("Point: toString") {
        Point2D p(1, 2);
        CHECK(p.toString() == "1, 2");
}

TEST_CASE("Point: equality operators") {
        Point2D a(1, 2);
        Point2D b(1, 2);
        Point2D c(3, 4);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Point: multiplication") {
        Point2D p1(1, 2);
        Point2D p2(5, 4);
        CHECK(p1 * p2 == Point2D(5, 8));
}

TEST_CASE("Point: distanceTo") {
        Point2Dd a(0.0, 0.0);
        Point2Dd b(3.0, 4.0);
        CHECK(a.distanceTo(b) == doctest::Approx(5.0));
}

TEST_CASE("Point: lerp") {
        Point2Dd a(0.0, 0.0);
        Point2Dd b(10.0, 20.0);
        auto mid = a.lerp(b, 0.5);
        CHECK(mid.x() == doctest::Approx(5.0));
        CHECK(mid.y() == doctest::Approx(10.0));
}

TEST_CASE("Point: lerp endpoints") {
        Point2Dd a(1.0, 2.0);
        Point2Dd b(5.0, 6.0);
        auto start = a.lerp(b, 0.0);
        auto end = a.lerp(b, 1.0);
        CHECK(start.x() == doctest::Approx(1.0));
        CHECK(end.x() == doctest::Approx(5.0));
}

TEST_CASE("Point: clamp") {
        Point2D p(15, -5);
        Point2D minP(0, 0);
        Point2D maxP(10, 10);
        auto clamped = p.clamp(minP, maxP);
        CHECK(clamped.x() == 10);
        CHECK(clamped.y() == 0);
}

TEST_CASE("Point: isWithinBounds") {
        Point2D p(5, 5);
        Point2D minP(0, 0);
        Point2D maxP(10, 10);
        CHECK(p.isWithinBounds(minP, maxP));
        Point2D outside(11, 5);
        CHECK_FALSE(outside.isWithinBounds(minP, maxP));
}

TEST_CASE("Point3D: construction and accessors") {
        Point3D p(1, 2, 3);
        CHECK(p.x() == 1);
        CHECK(p.y() == 2);
        CHECK(p.z() == 3);
}

TEST_CASE("Point3D: setZ") {
        Point3D p;
        p.setZ(42);
        CHECK(p.z() == 42);
}

TEST_CASE("Point3Dd: distanceTo 3D") {
        Point3Dd a(0.0, 0.0, 0.0);
        Point3Dd b(1.0, 2.0, 2.0);
        CHECK(a.distanceTo(b) == doctest::Approx(3.0));
}
