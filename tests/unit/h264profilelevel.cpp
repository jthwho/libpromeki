/**
 * @file      tests/h264profilelevel.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/h264profilelevel.h>
#include <promeki/enums_codec.h>

using namespace promeki;

TEST_CASE("H264ProfileLevel::profileFromWire maps the wire vocabulary") {
        CHECK(H264ProfileLevel::profileFromWire("") == H264Profile::Auto);
        CHECK(H264ProfileLevel::profileFromWire("baseline") == H264Profile::Baseline);
        CHECK(H264ProfileLevel::profileFromWire("main") == H264Profile::Main);
        CHECK(H264ProfileLevel::profileFromWire("high") == H264Profile::High);
        CHECK(H264ProfileLevel::profileFromWire("high10") == H264Profile::High10);
        CHECK(H264ProfileLevel::profileFromWire("high422") == H264Profile::High422);
        CHECK(H264ProfileLevel::profileFromWire("high444") == H264Profile::High444);
        CHECK(H264ProfileLevel::profileFromWire("progressive") == H264Profile::ProgressiveHigh);
        // Unrecognised token falls back to Auto (geometry-derived).
        CHECK(H264ProfileLevel::profileFromWire("nonsense") == H264Profile::Auto);
}

TEST_CASE("H264ProfileLevel::profileToWire round-trips x264 tokens") {
        CHECK(H264ProfileLevel::profileToWire(H264Profile::High444) == "high444");
        CHECK(H264ProfileLevel::profileToWire(H264Profile::High422) == "high422");
        CHECK(H264ProfileLevel::profileToWire(H264Profile::High10) == "high10");
        CHECK(H264ProfileLevel::profileToWire(H264Profile::Baseline) == "baseline");
        // ProgressiveHigh has no distinct x264 token — maps to "high".
        CHECK(H264ProfileLevel::profileToWire(H264Profile::ProgressiveHigh) == "high");
        // Auto → empty (no profile constraint).
        CHECK(H264ProfileLevel::profileToWire(H264Profile::Auto).isEmpty());
}

TEST_CASE("H264Profile is a registered TypedEnum (toString / fromString)") {
        // The enum identifiers are CamelCase, independent of the lowercase
        // wire tokens handled by H264ProfileLevel.
        CHECK(H264Profile::High422.toString() == "High422");
        CHECK(H264Profile(String("High444")) == H264Profile::High444);
}

TEST_CASE("H264ProfileLevel::autoProfile derives from geometry") {
        CHECK(H264ProfileLevel::autoProfile(3, 8) == H264Profile::High444);
        CHECK(H264ProfileLevel::autoProfile(2, 8) == H264Profile::High422);
        CHECK(H264ProfileLevel::autoProfile(1, 10) == H264Profile::High10);
        CHECK(H264ProfileLevel::autoProfile(1, 8) == H264Profile::High);
}

TEST_CASE("H264ProfileLevel::levelIdc returns level_idc, 0 for auto") {
        CHECK(H264ProfileLevel::levelIdc("") == 0);
        CHECK(H264ProfileLevel::levelIdc("nonsense") == 0);
        CHECK(H264ProfileLevel::levelIdc("1") == 10);
        CHECK(H264ProfileLevel::levelIdc("1.0") == 10);
        CHECK(H264ProfileLevel::levelIdc("1b") == 9); // x264 / NV_ENC sentinel for level 1b
        CHECK(H264ProfileLevel::levelIdc("3.1") == 31);
        CHECK(H264ProfileLevel::levelIdc("4.1") == 41);
        CHECK(H264ProfileLevel::levelIdc("5") == 50);
        CHECK(H264ProfileLevel::levelIdc("6.2") == 62);
}
