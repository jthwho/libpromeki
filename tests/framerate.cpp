/**
 * @file      framerate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/framerate.h>

using namespace promeki;

TEST_CASE("FrameRate: default construction is invalid") {
        FrameRate fr;
        CHECK_FALSE(fr.isValid());
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: construction from well-known rate 24") {
        FrameRate fr(FrameRate::FPS_24);
        CHECK(fr.isValid());
        CHECK(fr.isWellKnownRate());
        CHECK(fr.wellKnownRate() == FrameRate::FPS_24);
        CHECK(fr.numerator() == 24);
        CHECK(fr.denominator() == 1);
        CHECK(fr.toDouble() == doctest::Approx(24.0));
}

TEST_CASE("FrameRate: construction from well-known rate 25") {
        FrameRate fr(FrameRate::FPS_25);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 25);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 29.97") {
        FrameRate fr(FrameRate::FPS_2997);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30000);
        CHECK(fr.denominator() == 1001);
        CHECK(fr.toDouble() == doctest::Approx(29.97).epsilon(0.01));
}

TEST_CASE("FrameRate: construction from well-known rate 30") {
        FrameRate fr(FrameRate::FPS_30);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 59.94") {
        FrameRate fr(FrameRate::FPS_5994);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 60") {
        FrameRate fr(FrameRate::FPS_60);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 23.98") {
        FrameRate fr(FrameRate::FPS_2398);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 24000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 50") {
        FrameRate fr(FrameRate::FPS_50);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 50);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from rational") {
        FrameRate::RationalType r(48, 1);
        FrameRate fr(r);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 48);
        CHECK(fr.denominator() == 1);
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: invalid well-known rate") {
        FrameRate fr(FrameRate::FPS_Invalid);
        CHECK_FALSE(fr.isValid());
}

TEST_CASE("FrameRate: toString") {
        FrameRate fr(FrameRate::FPS_24);
        String s = fr.toString();
        CHECK_FALSE(s.isEmpty());
}
