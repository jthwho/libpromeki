/**
 * @file      size2d.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/size2d.h>

using namespace promeki;

TEST_CASE("Size2Du32: default construction") {
        Size2Du32 s;
        CHECK(s.width() == 0);
        CHECK(s.height() == 0);
        CHECK_FALSE(s.isValid());
}

TEST_CASE("Size2Du32: construction with values") {
        Size2Du32 s(1920, 1080);
        CHECK(s.width() == 1920);
        CHECK(s.height() == 1080);
        CHECK(s.isValid());
}

TEST_CASE("Size2Du32: setters") {
        Size2Du32 s;
        s.setWidth(640);
        s.setHeight(480);
        CHECK(s.width() == 640);
        CHECK(s.height() == 480);
}

TEST_CASE("Size2Du32: set both") {
        Size2Du32 s;
        s.set(1280, 720);
        CHECK(s.width() == 1280);
        CHECK(s.height() == 720);
}

TEST_CASE("Size2Du32: area") {
        Size2Du32 s(100, 200);
        CHECK(s.area() == 20000);
}

TEST_CASE("Size2Du32: isValid with zero dimension") {
        Size2Du32 a(0, 100);
        Size2Du32 b(100, 0);
        CHECK_FALSE(a.isValid());
        CHECK_FALSE(b.isValid());
}

TEST_CASE("Size2Du32: toString") {
        Size2Du32 s(1920, 1080);
        CHECK(s.toString() == "1920x1080");
}

TEST_CASE("Size2Du32: String conversion operator") {
        Size2Du32 s(640, 480);
        String str = s;
        CHECK(str == "640x480");
}

TEST_CASE("Size2Du32: toString output") {
        Size2Du32 s(3840, 2160);
        CHECK(s.toString() == "3840x2160");
}

TEST_CASE("Size2Du32: pointIsInside") {
        Size2Du32 s(100, 100);
        Point2Di32 inside(50, 50);
        Point2Di32 outside(150, 50);
        Point2Di32 edge(0, 0);
        Point2Di32 boundary(100, 50);
        CHECK(s.pointIsInside(inside));
        CHECK_FALSE(s.pointIsInside(outside));
        CHECK(s.pointIsInside(edge));
        CHECK_FALSE(s.pointIsInside(boundary));
}

TEST_CASE("Size2Df: float specialization") {
        Size2Df s(19.20f, 10.80f);
        CHECK(s.width() == doctest::Approx(19.20f));
        CHECK(s.height() == doctest::Approx(10.80f));
        CHECK(s.isValid());
}

TEST_CASE("Size2Dd: double specialization") {
        Size2Dd s(1920.5, 1080.5);
        CHECK(s.width() == doctest::Approx(1920.5));
        CHECK(s.height() == doctest::Approx(1080.5));
}

// ============================================================================
// Equality operators
// ============================================================================

TEST_CASE("Size2Du32: equality equal") {
        Size2Du32 a(1920, 1080);
        Size2Du32 b(1920, 1080);
        CHECK(a == b);
        CHECK_FALSE(a != b);
}

TEST_CASE("Size2Du32: equality different width") {
        Size2Du32 a(1920, 1080);
        Size2Du32 b(1280, 1080);
        CHECK_FALSE(a == b);
        CHECK(a != b);
}

TEST_CASE("Size2Du32: equality different height") {
        Size2Du32 a(1920, 1080);
        Size2Du32 b(1920, 720);
        CHECK_FALSE(a == b);
        CHECK(a != b);
}

TEST_CASE("Size2Du32: equality default constructed") {
        Size2Du32 a;
        Size2Du32 b;
        CHECK(a == b);
}

TEST_CASE("Size2Di32: equality") {
        Size2Di32 a(640, 480);
        Size2Di32 b(640, 480);
        Size2Di32 c(640, 240);
        CHECK(a == b);
        CHECK(a != c);
}
