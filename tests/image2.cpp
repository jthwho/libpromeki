/**
 * @file      image2.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/image.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("Image_Default") {
    Image img;
    CHECK(!img.isValid());
    CHECK(img.referenceCount() == 1);
}

// ============================================================================
// Construction with desc
// ============================================================================

TEST_CASE("Image_Construct") {
    Image img(1920, 1080, PixelFormat::RGBA8);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelFormatID() == PixelFormat::RGBA8);
    CHECK(img.referenceCount() == 1);
}

TEST_CASE("Image_ConstructFromDesc") {
    ImageDesc desc(1920, 1080, PixelFormat::RGB8);
    Image img(desc);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.pixelFormatID() == PixelFormat::RGB8);
}

// ============================================================================
// Fill
// ============================================================================

TEST_CASE("Image_Fill") {
    Image img(64, 64, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    CHECK(img.fill(0xAB));

    const uint8_t *ptr = static_cast<const uint8_t *>(img.data());
    CHECK(ptr[0] == 0xAB);
    CHECK(ptr[1] == 0xAB);
}

// ============================================================================
// Plane access
// ============================================================================

TEST_CASE("Image_Plane") {
    Image img(64, 64, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    Buffer p = img.plane(0);
    CHECK(p.isValid());
    CHECK(p.data() != nullptr);
}

// ============================================================================
// Shared copy (COW)
// ============================================================================

TEST_CASE("Image_SharedCopy") {
    Image img1(64, 64, PixelFormat::RGBA8);
    Image img2 = img1;
    CHECK(img1.referenceCount() == 2);
    CHECK(img2.referenceCount() == 2);
    CHECK(img1.width() == img2.width());
    CHECK(img1.height() == img2.height());
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("Image_Metadata") {
    Image img(64, 64, PixelFormat::RGBA8);
    CHECK(img.metadata().isEmpty());

    img.metadata().set(Metadata::Title, String("Test Image"));
    CHECK(!img.metadata().isEmpty());
    CHECK(img.metadata().get(Metadata::Title).get<String>() == "Test Image");
}

// ============================================================================
// Line stride
// ============================================================================

TEST_CASE("Image_LineStride") {
    Image img(1920, 1080, PixelFormat::RGBA8);
    CHECK(img.lineStride() == 1920 * 4);
}
