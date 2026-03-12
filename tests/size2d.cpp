/**
 * @file      size2d.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <sstream>
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

TEST_CASE("Size2Du32: ostream operator") {
        Size2Du32 s(3840, 2160);
        std::ostringstream os;
        os << s;
        CHECK(os.str() == "3840x2160");
}

TEST_CASE("Size2Du32: istream operator") {
        std::istringstream is("1920x1080");
        Size2Du32 s;
        is >> s;
        CHECK(s.width() == 1920);
        CHECK(s.height() == 1080);
}

TEST_CASE("Size2Du32: istream operator with bad input") {
        std::istringstream is("bad");
        Size2Du32 s(100, 100);
        is >> s;
        CHECK(is.fail());
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
