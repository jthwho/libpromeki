/**
 * @file      xyzcolor.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/xyzcolor.h>

using namespace promeki;

TEST_CASE("XYZColor: default construction is invalid") {
        XYZColor c;
        CHECK_FALSE(c.isValid());
}

TEST_CASE("XYZColor: construction with values") {
        XYZColor c(0.5, 0.3, 0.2);
        CHECK(c.isValid());
        CHECK(c.x() == doctest::Approx(0.5));
        CHECK(c.y() == doctest::Approx(0.3));
        CHECK(c.z() == doctest::Approx(0.2));
}

TEST_CASE("XYZColor: construction from DataType") {
        XYZColor::DataType d(0.1, 0.2, 0.3);
        XYZColor           c(d);
        CHECK(c.isValid());
}

TEST_CASE("XYZColor: setters") {
        XYZColor c;
        c.setX(0.5);
        c.setY(0.3);
        c.setZ(0.2);
        CHECK(c.isValid());
        CHECK(c.x() == doctest::Approx(0.5));
}

TEST_CASE("XYZColor: set all at once") {
        XYZColor c;
        c.set(0.1, 0.2, 0.3);
        CHECK(c.isValid());
        CHECK(c.x() == doctest::Approx(0.1));
        CHECK(c.y() == doctest::Approx(0.2));
        CHECK(c.z() == doctest::Approx(0.3));
}

TEST_CASE("XYZColor: isValid with negative values") {
        XYZColor a(1.5, 0.5, 0.5);
        CHECK(a.isValid()); // CIE XYZ components can exceed 1.0
        XYZColor b(0.5, -0.1, 0.5);
        CHECK_FALSE(b.isValid()); // Negative values are invalid
}

TEST_CASE("XYZColor: isValid with boundary values") {
        XYZColor a(0.0, 0.0, 0.0);
        CHECK(a.isValid());
        XYZColor b(1.0, 1.0, 1.0);
        CHECK(b.isValid());
}

TEST_CASE("XYZColor: lerp") {
        XYZColor a(0.0, 0.0, 0.0);
        XYZColor b(1.0, 1.0, 1.0);
        XYZColor mid = a.lerp(b, 0.5);
        CHECK(mid.x() == doctest::Approx(0.5));
        CHECK(mid.y() == doctest::Approx(0.5));
        CHECK(mid.z() == doctest::Approx(0.5));
}

TEST_CASE("XYZColor: toString") {
        XYZColor c(0.5, 0.3, 0.2);
        String   s = c.toString();
        CHECK_FALSE(s.isEmpty());
        CHECK(s.find('X') != String::npos);
}

TEST_CASE("XYZColor: data accessor") {
        XYZColor    c(0.1, 0.2, 0.3);
        const auto &d = c.data();
        CHECK(d[0] == doctest::Approx(0.1));
        CHECK(d[1] == doctest::Approx(0.2));
        CHECK(d[2] == doctest::Approx(0.3));
}
