/**
 * @file      point.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/point.h>

using namespace promeki;

TEST_CASE("Point: default construction") {
        Point2Di32 p;
        CHECK(p.x() == 0);
        CHECK(p.y() == 0);
}

TEST_CASE("Point: construction with values") {
        Point2Di32 p(3, 7);
        CHECK(p.x() == 3);
        CHECK(p.y() == 7);
}

TEST_CASE("Point: setX and setY") {
        Point2Di32 p;
        p.setX(10);
        p.setY(20);
        CHECK(p.x() == 10);
        CHECK(p.y() == 20);
}

TEST_CASE("Point: toString") {
        Point2Di32 p(1, 2);
        CHECK(p.toString() == "1, 2");
}

TEST_CASE("Point: equality operators") {
        Point2Di32 a(1, 2);
        Point2Di32 b(1, 2);
        Point2Di32 c(3, 4);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Point: multiplication") {
        Point2Di32 p1(1, 2);
        Point2Di32 p2(5, 4);
        CHECK(p1 * p2 == Point2Di32(5, 8));
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
        Point2Di32 p(15, -5);
        Point2Di32 minP(0, 0);
        Point2Di32 maxP(10, 10);
        auto clamped = p.clamp(minP, maxP);
        CHECK(clamped.x() == 10);
        CHECK(clamped.y() == 0);
}

TEST_CASE("Point: isWithinBounds") {
        Point2Di32 p(5, 5);
        Point2Di32 minP(0, 0);
        Point2Di32 maxP(10, 10);
        CHECK(p.isWithinBounds(minP, maxP));
        Point2Di32 outside(11, 5);
        CHECK_FALSE(outside.isWithinBounds(minP, maxP));
}

TEST_CASE("Point3Di32: construction and accessors") {
        Point3Di32 p(1, 2, 3);
        CHECK(p.x() == 1);
        CHECK(p.y() == 2);
        CHECK(p.z() == 3);
}

TEST_CASE("Point3Di32: setZ") {
        Point3Di32 p;
        p.setZ(42);
        CHECK(p.z() == 42);
}

TEST_CASE("Point3Dd: distanceTo 3D") {
        Point3Dd a(0.0, 0.0, 0.0);
        Point3Dd b(1.0, 2.0, 2.0);
        CHECK(a.distanceTo(b) == doctest::Approx(3.0));
}
