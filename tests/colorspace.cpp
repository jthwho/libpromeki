/**
 * @file      colorspace.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/colorspace.h>

using namespace promeki;

TEST_CASE("ColorSpace: default construction is invalid") {
        ColorSpace cs;
        CHECK(cs.id() == ColorSpace::Invalid);
}

TEST_CASE("ColorSpace: Rec709 construction") {
        ColorSpace cs(ColorSpace::Rec709);
        CHECK(cs.id() == ColorSpace::Rec709);
        CHECK_FALSE(cs.name().isEmpty());
}

TEST_CASE("ColorSpace: Rec709 has valid primaries") {
        ColorSpace cs(ColorSpace::Rec709);
        CHECK(cs.red().isValid());
        CHECK(cs.green().isValid());
        CHECK(cs.blue().isValid());
}

TEST_CASE("ColorSpace: Rec709 white point is valid") {
        ColorSpace cs(ColorSpace::Rec709);
        CHECK(cs.whitePoint().isValid());
}

TEST_CASE("ColorSpace: Rec709 transfer function") {
        ColorSpace cs(ColorSpace::Rec709);
        // 0 input should give 0 output
        CHECK(cs.transferFunc(0.0) == doctest::Approx(0.0).epsilon(0.001));
        // 1 input should give 1 output
        CHECK(cs.transferFunc(1.0) == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("ColorSpace: LinearRec709 construction") {
        ColorSpace cs(ColorSpace::LinearRec709);
        CHECK(cs.id() == ColorSpace::LinearRec709);
}

TEST_CASE("ColorSpace: inverse color space") {
        ColorSpace cs(ColorSpace::Rec709);
        ColorSpace inv = cs.inverseColorSpace();
        CHECK(inv.id() == ColorSpace::LinearRec709);
}

TEST_CASE("ColorSpace: Rec601_PAL") {
        ColorSpace cs(ColorSpace::Rec601_PAL);
        CHECK(cs.id() == ColorSpace::Rec601_PAL);
        CHECK_FALSE(cs.name().isEmpty());
}

TEST_CASE("ColorSpace: Rec601_NTSC") {
        ColorSpace cs(ColorSpace::Rec601_NTSC);
        CHECK(cs.id() == ColorSpace::Rec601_NTSC);
        CHECK_FALSE(cs.name().isEmpty());
}
