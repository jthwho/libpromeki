/**
 * @file      image2.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/image.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("Image_Default") {
    Image img;
    CHECK(!img.isValid());
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
    CHECK(img.fill(0xAB).isOk());

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
    const Buffer::Ptr &p = img.plane(0);
    CHECK(p->isValid());
    CHECK(p->data() != nullptr);
}

// ============================================================================
// Shared copy (COW)
// ============================================================================

TEST_CASE("Image_Copy") {
    Image img1(64, 64, PixelFormat::RGBA8);
    Image img2 = img1;
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

// ============================================================================
// Construction from Size2Du32
// ============================================================================

TEST_CASE("Image_ConstructFromSize2D") {
    Size2Du32 sz(640, 480);
    Image img(sz, PixelFormat::RGBA8);
    CHECK(img.isValid());
    CHECK(img.width() == 640);
    CHECK(img.height() == 480);
    CHECK(img.size().width() == sz.width());
    CHECK(img.size().height() == sz.height());
}

// ============================================================================
// Desc accessor
// ============================================================================

TEST_CASE("Image_DescAccessor") {
    Image img(1280, 720, PixelFormat::RGB8);
    REQUIRE(img.isValid());
    const ImageDesc &desc = img.desc();
    CHECK(desc.isValid());
    CHECK(desc.width() == 1280);
    CHECK(desc.height() == 720);
    CHECK(desc.pixelFormatID() == PixelFormat::RGB8);
}

// ============================================================================
// PixelFormat accessor
// ============================================================================

TEST_CASE("Image_PixelFormat") {
    Image img(64, 64, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    const PixelFormat *pf = img.pixelFormat();
    REQUIRE(pf != nullptr);
    CHECK(pf->id() == PixelFormat::RGBA8);
    CHECK(pf->isValid());
}

// ============================================================================
// Planes list
// ============================================================================

TEST_CASE("Image_Planes") {
    Image img(64, 64, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    const Buffer::PtrList &pl = img.planes();
    CHECK(pl.size() >= 1);
    CHECK(pl[0]->isValid());
}

// ============================================================================
// Data pointer
// ============================================================================

TEST_CASE("Image_DataPointer") {
    Image img(32, 32, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    void *ptr = img.data();
    CHECK(ptr != nullptr);
    CHECK(ptr == img.plane(0)->data());
}

// ============================================================================
// Default image is not valid, fill returns false
// ============================================================================

TEST_CASE("Image_FillInvalid") {
    Image img;
    CHECK(!img.isValid());
    CHECK(img.fill(0).isError());
}

// ============================================================================
// RGB8 pixel format and line stride
// ============================================================================

TEST_CASE("Image_RGB8_LineStride") {
    Image img(100, 100, PixelFormat::RGB8);
    REQUIRE(img.isValid());
    CHECK(img.lineStride() == 100 * 3);
    CHECK(img.pixelFormatID() == PixelFormat::RGB8);
}

// ============================================================================
// PaintEngine creation
// ============================================================================

TEST_CASE("Image_CreatePaintEngine") {
    Image img(64, 64, PixelFormat::RGBA8);
    REQUIRE(img.isValid());
    PaintEngine pe = img.createPaintEngine();
    CHECK(pe.pixelFormat() != nullptr);
}
