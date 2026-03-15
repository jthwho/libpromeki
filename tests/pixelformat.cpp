/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/pixelformat.h>

using namespace promeki;

TEST_CASE("PixelFormat: lookup Invalid returns null or invalid") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::Invalid);
        if(pf != nullptr) {
                CHECK_FALSE(pf->isValid());
        }
}

TEST_CASE("PixelFormat: lookup RGBA8") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->isValid());
        CHECK(pf->id() == PixelFormat::RGBA8);
}

TEST_CASE("PixelFormat: RGBA8 name is non-empty") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK_FALSE(pf->name().isEmpty());
}

TEST_CASE("PixelFormat: RGBA8 has alpha") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->hasAlpha());
}

TEST_CASE("PixelFormat: RGB8 does not have alpha") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGB8);
        REQUIRE(pf != nullptr);
        CHECK_FALSE(pf->hasAlpha());
}

TEST_CASE("PixelFormat: RGBA8 bytes per block") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->bytesPerBlock() > 0);
        CHECK(pf->pixelsPerBlock() > 0);
}

TEST_CASE("PixelFormat: RGBA8 sampling is 444") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->sampling() == PixelFormat::Sampling444);
}

TEST_CASE("PixelFormat: RGBA8 plane count") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->planeCount() == 1);
}

TEST_CASE("PixelFormat: RGB8 is valid and not alpha") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGB8);
        REQUIRE(pf != nullptr);
        CHECK(pf->isValid());
        CHECK_FALSE(pf->hasAlpha());
}

TEST_CASE("PixelFormat: RGBA8 is not compressed") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK_FALSE(pf->compressed());
}

TEST_CASE("PixelFormat: JPEG_RGBA8 is compressed") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->compressed());
}

TEST_CASE("PixelFormat: plane count") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->planeCount() >= 1);
}
