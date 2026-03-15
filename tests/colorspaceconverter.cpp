/**
 * @file      colorspaceconverter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/colorspaceconverter.h>

using namespace promeki;

TEST_CASE("CSC: RGB_to_YCbCr_Rec709 matrix row 0 sums to ~1") {
        float sum = RGB_to_YCbCr_Rec709.matrix[0][0] +
                    RGB_to_YCbCr_Rec709.matrix[0][1] +
                    RGB_to_YCbCr_Rec709.matrix[0][2];
        CHECK(sum == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("CSC: RGB_to_YCbCr_Rec709 luma coefficients") {
        CHECK(RGB_to_YCbCr_Rec709.matrix[0][0] == doctest::Approx(0.2126f));
        CHECK(RGB_to_YCbCr_Rec709.matrix[0][1] == doctest::Approx(0.7152f));
        CHECK(RGB_to_YCbCr_Rec709.matrix[0][2] == doctest::Approx(0.0722f));
}

TEST_CASE("CSC: YCbCr_Rec709_to_RGB luma row") {
        CHECK(YCbCr_Rec709_to_RGB.matrix[0][0] == doctest::Approx(1.0f));
        CHECK(YCbCr_Rec709_to_RGB.matrix[0][1] == doctest::Approx(0.0f));
}

TEST_CASE("CSC: RGB_to_YCbCr_Rec709 offset values") {
        CHECK(RGB_to_YCbCr_Rec709.offset[0] == doctest::Approx(16.0f / 255.0f));
        CHECK(RGB_to_YCbCr_Rec709.offset[1] == doctest::Approx(128.0f / 255.0f));
        CHECK(RGB_to_YCbCr_Rec709.offset[2] == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("CSC: YCbCr_Rec709_to_RGB offset values") {
        CHECK(YCbCr_Rec709_to_RGB.offset[0] == doctest::Approx(-16.0f / 255.0f));
        CHECK(YCbCr_Rec709_to_RGB.offset[1] == doctest::Approx(-128.0f / 255.0f));
        CHECK(YCbCr_Rec709_to_RGB.offset[2] == doctest::Approx(-128.0f / 255.0f));
}

TEST_CASE("CSC: Cb row sums to ~0") {
        float sum = RGB_to_YCbCr_Rec709.matrix[1][0] +
                    RGB_to_YCbCr_Rec709.matrix[1][1] +
                    RGB_to_YCbCr_Rec709.matrix[1][2];
        CHECK(sum == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("CSC: Cr row sums to ~0") {
        float sum = RGB_to_YCbCr_Rec709.matrix[2][0] +
                    RGB_to_YCbCr_Rec709.matrix[2][1] +
                    RGB_to_YCbCr_Rec709.matrix[2][2];
        CHECK(sum == doctest::Approx(0.0f).epsilon(0.001f));
}
