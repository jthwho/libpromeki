/**
 * @file      ciepoint.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/ciepoint.h>

using namespace promeki;

TEST_CASE("CIEPoint: default construction is invalid") {
        CIEPoint p;
        CHECK_FALSE(p.isValid());
}

TEST_CASE("CIEPoint: construction with valid values") {
        CIEPoint p(0.3127, 0.3290);
        CHECK(p.isValid());
}

TEST_CASE("CIEPoint: construction with out of range values") {
        CIEPoint p(0.9, 0.5);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("CIEPoint: isValidWavelength boundaries") {
        CHECK_FALSE(CIEPoint::isValidWavelength(359.0));
        CHECK(CIEPoint::isValidWavelength(360.0));
        CHECK(CIEPoint::isValidWavelength(550.0));
        CHECK(CIEPoint::isValidWavelength(700.0));
        CHECK_FALSE(CIEPoint::isValidWavelength(701.0));
}

TEST_CASE("CIEPoint: wavelengthToXYZ returns valid color") {
        XYZColor xyz = CIEPoint::wavelengthToXYZ(550.0);
        CHECK(xyz.isValid());
}

TEST_CASE("CIEPoint: wavelengthToCIEPoint returns valid point") {
        CIEPoint p = CIEPoint::wavelengthToCIEPoint(550.0);
        CHECK(p.isValid());
}

TEST_CASE("CIEPoint: colorTempToWhitePoint valid range") {
        CIEPoint d55 = CIEPoint::colorTempToWhitePoint(5500.0);
        CHECK(d55.isValid());
        CIEPoint d65 = CIEPoint::colorTempToWhitePoint(6500.0);
        CHECK(d65.isValid());
}

TEST_CASE("CIEPoint: colorTempToWhitePoint too low is invalid") {
        CIEPoint p = CIEPoint::colorTempToWhitePoint(3000.0);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("CIEPoint: colorTempToWhitePoint too high is invalid") {
        CIEPoint p = CIEPoint::colorTempToWhitePoint(30000.0);
        CHECK_FALSE(p.isValid());
}

TEST_CASE("CIEPoint: lerp") {
        CIEPoint a(0.2, 0.3);
        CIEPoint b(0.4, 0.5);
        CIEPoint mid = a.lerp(b, 0.5);
        CHECK(mid.isValid());
}

TEST_CASE("CIEPoint: construction from DataType") {
        CIEPoint::DataType d(0.3, 0.3);
        CIEPoint p(d);
        CHECK(p.isValid());
}
