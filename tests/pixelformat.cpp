/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/core/metadata.h>

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
        CHECK_FALSE(pf->isCompressed());
}

TEST_CASE("PixelFormat: JPEG_RGBA8 is compressed") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->isCompressed());
}

TEST_CASE("PixelFormat: plane count") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->planeCount() >= 1);
}

TEST_CASE("PixelFormat: RGBA8 compCount is 4") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->compCount() == 4);
        CHECK(pf->compDesc(0).type == PixelFormat::Comp0);
        CHECK(pf->compDesc(1).type == PixelFormat::Comp1);
        CHECK(pf->compDesc(2).type == PixelFormat::Comp2);
        CHECK(pf->compDesc(3).type == PixelFormat::CompAlpha);
        for(size_t i = 0; i < pf->compCount(); i++) {
                CHECK(pf->compDesc(i).bits == 8);
                CHECK(pf->compDesc(i).plane == 0);
        }
}

TEST_CASE("PixelFormat: RGB8 compCount is 3") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGB8);
        REQUIRE(pf != nullptr);
        CHECK(pf->compCount() == 3);
        CHECK(pf->compDesc(0).type == PixelFormat::Comp0);
        CHECK(pf->compDesc(1).type == PixelFormat::Comp1);
        CHECK(pf->compDesc(2).type == PixelFormat::Comp2);
}

TEST_CASE("PixelFormat: JPEG_RGBA8 compCount is 4") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGBA8);
        REQUIRE(pf != nullptr);
        CHECK(pf->compCount() == 4);
}

TEST_CASE("PixelFormat: JPEG_RGB8 compCount is 3") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGB8);
        REQUIRE(pf != nullptr);
        CHECK(pf->compCount() == 3);
        CHECK(pf->compDesc(0).type == PixelFormat::Comp0);
        CHECK(pf->compDesc(1).type == PixelFormat::Comp1);
        CHECK(pf->compDesc(2).type == PixelFormat::Comp2);
        for(size_t i = 0; i < pf->compCount(); i++) {
                CHECK(pf->compDesc(i).bits == 8);
                CHECK(pf->compDesc(i).plane == 0);
        }
}

TEST_CASE("PixelFormat: RGB8 is not compressed") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::RGB8);
        REQUIRE(pf != nullptr);
        CHECK_FALSE(pf->isCompressed());
}

TEST_CASE("PixelFormat: JPEG_RGB8 is compressed") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGB8);
        REQUIRE(pf != nullptr);
        CHECK(pf->isCompressed());
}

TEST_CASE("PixelFormat: JPEG planeSize is 0 without CompressedSize metadata") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGB8);
        REQUIRE(pf != nullptr);
        // ImageDesc with no CompressedSize in metadata
        ImageDesc desc(640, 480, PixelFormat::JPEG_RGB8);
        CHECK(pf->planeSize(0, desc) == 0);
}

TEST_CASE("PixelFormat: JPEG planeSize reads CompressedSize from metadata") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGB8);
        REQUIRE(pf != nullptr);
        ImageDesc desc(640, 480, PixelFormat::JPEG_RGB8);
        desc.metadata().set(Metadata::CompressedSize, 12345);
        CHECK(pf->planeSize(0, desc) == 12345);
}

TEST_CASE("PixelFormat: JPEG lineStride is 0") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_RGB8);
        REQUIRE(pf != nullptr);
        ImageDesc desc(640, 480, PixelFormat::JPEG_RGB8);
        CHECK(pf->lineStride(0, desc) == 0);
}

TEST_CASE("PixelFormat: JPEG_YUV8_422 compCount is 3") {
        const PixelFormat *pf = PixelFormat::lookup(PixelFormat::JPEG_YUV8_422);
        REQUIRE(pf != nullptr);
        CHECK(pf->compCount() == 3);
        CHECK(pf->compDesc(0).type == PixelFormat::Comp0);
        CHECK(pf->compDesc(1).type == PixelFormat::Comp1);
        CHECK(pf->compDesc(2).type == PixelFormat::Comp2);
}
