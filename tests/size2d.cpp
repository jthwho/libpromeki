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

TEST_CASE("Size2D: default construction") {
        Size2D s;
        CHECK(s.width() == 0);
        CHECK(s.height() == 0);
        CHECK_FALSE(s.isValid());
}

TEST_CASE("Size2D: construction with values") {
        Size2D s(1920, 1080);
        CHECK(s.width() == 1920);
        CHECK(s.height() == 1080);
        CHECK(s.isValid());
}

TEST_CASE("Size2D: setters") {
        Size2D s;
        s.setWidth(640);
        s.setHeight(480);
        CHECK(s.width() == 640);
        CHECK(s.height() == 480);
}

TEST_CASE("Size2D: set both") {
        Size2D s;
        s.set(1280, 720);
        CHECK(s.width() == 1280);
        CHECK(s.height() == 720);
}

TEST_CASE("Size2D: area") {
        Size2D s(100, 200);
        CHECK(s.area() == 20000);
}

TEST_CASE("Size2D: isValid with zero dimension") {
        Size2D a(0, 100);
        Size2D b(100, 0);
        CHECK_FALSE(a.isValid());
        CHECK_FALSE(b.isValid());
}

TEST_CASE("Size2D: toString") {
        Size2D s(1920, 1080);
        CHECK(s.toString() == "1920x1080");
}

TEST_CASE("Size2D: String conversion operator") {
        Size2D s(640, 480);
        String str = s;
        CHECK(str == "640x480");
}

TEST_CASE("Size2D: ostream operator") {
        Size2D s(3840, 2160);
        std::ostringstream os;
        os << s;
        CHECK(os.str() == "3840x2160");
}

TEST_CASE("Size2D: istream operator") {
        std::istringstream is("1920x1080");
        Size2D s;
        is >> s;
        CHECK(s.width() == 1920);
        CHECK(s.height() == 1080);
}

TEST_CASE("Size2D: istream operator with bad input") {
        std::istringstream is("bad");
        Size2D s(100, 100);
        is >> s;
        CHECK(is.fail());
}

TEST_CASE("Size2D: pointIsInside") {
        Size2D s(100, 100);
        Point2D inside(50, 50);
        Point2D outside(150, 50);
        Point2D edge(0, 0);
        Point2D boundary(100, 50);
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
